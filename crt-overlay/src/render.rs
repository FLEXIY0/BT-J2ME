//! GL side: captured desktop texture -> shrink to `lines` rows -> librashader
//! chain -> window. CRT shaders draw one scanline per input row, so the
//! number of input rows is the number of scanlines on screen.
//!
//! Orientation: every texture here stores image rows top-first (row 0 = top
//! of the screen), which is what both capture back-ends produce. The flip to
//! GL's bottom-up window coordinates happens once, in the final blit.

use anyhow::{Context, Result, anyhow};
use glow::HasContext;
use librashader::presets::context::VideoDriver;
use librashader::presets::{ShaderFeatures, ShaderPreset};
use librashader::runtime::gl::{FilterChain, FilterChainOptions, FrameOptions, GLImage};
use librashader::runtime::{FilterChainParameters, Size, Viewport};
use std::collections::BTreeMap;
use std::path::Path;
use std::sync::Arc;

/// A colour texture with a framebuffer attached to it.
pub struct Target {
    pub tex: glow::Texture,
    pub fbo: glow::Framebuffer,
    pub width: u32,
    pub height: u32,
}

impl Target {
    pub fn new(gl: &glow::Context, width: u32, height: u32) -> Result<Target> {
        unsafe {
            let tex = gl.create_texture().map_err(|e| anyhow!(e))?;
            gl.bind_texture(glow::TEXTURE_2D, Some(tex));
            // Plain TexImage2D (not immutable storage): works on OpenGL 2.1 too.
            gl.tex_image_2d(
                glow::TEXTURE_2D,
                0,
                glow::RGBA8 as i32,
                width as i32,
                height as i32,
                0,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelUnpackData::Slice(None),
            );
            for (p, v) in [
                (glow::TEXTURE_MIN_FILTER, glow::LINEAR),
                (glow::TEXTURE_MAG_FILTER, glow::LINEAR),
                (glow::TEXTURE_WRAP_S, glow::CLAMP_TO_EDGE),
                (glow::TEXTURE_WRAP_T, glow::CLAMP_TO_EDGE),
                // Exactly one mip level: a sampler with mipmap filtering
                // (librashader uses them) must not see an incomplete texture.
                (glow::TEXTURE_BASE_LEVEL, 0),
                (glow::TEXTURE_MAX_LEVEL, 0),
            ] {
                gl.tex_parameter_i32(glow::TEXTURE_2D, p, v as i32);
            }
            if crate::glutil::modern(gl) {
                // Captured pixels can carry garbage in the X/alpha byte (X11
                // depth-24 windows). The desktop is opaque, so read alpha as 1.
                // (The lite renderer ignores alpha itself.)
                gl.tex_parameter_i32(glow::TEXTURE_2D, glow::TEXTURE_SWIZZLE_A, glow::ONE as i32);
            }
            let fbo = gl.create_framebuffer().map_err(|e| anyhow!(e))?;
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(fbo));
            gl.framebuffer_texture_2d(
                glow::FRAMEBUFFER,
                glow::COLOR_ATTACHMENT0,
                glow::TEXTURE_2D,
                Some(tex),
                0,
            );
            let status = gl.check_framebuffer_status(glow::FRAMEBUFFER);
            gl.clear_color(0.0, 0.0, 0.0, 1.0);
            gl.clear(glow::COLOR_BUFFER_BIT);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.bind_texture(glow::TEXTURE_2D, None);
            if status != glow::FRAMEBUFFER_COMPLETE {
                return Err(anyhow!("framebuffer incomplete: {status:#x}"));
            }
            Ok(Target {
                tex,
                fbo,
                width,
                height,
            })
        }
    }

    pub fn image(&self) -> GLImage {
        GLImage {
            handle: Some(self.tex),
            format: glow::RGBA8,
            size: Size::new(self.width, self.height),
        }
    }

    pub fn destroy(&self, gl: &glow::Context) {
        unsafe {
            gl.delete_framebuffer(self.fbo);
            gl.delete_texture(self.tex);
        }
    }
}

pub struct Renderer {
    gl: Arc<glow::Context>,
    /// Full-resolution capture of the monitor, written by the capture back-end.
    pub desktop: Target,
    /// Half-size step (proper 2x2 averaging) and the low-resolution shader
    /// input with `lines` rows.
    half: Option<Target>,
    small: Option<Target>,
    /// Shader output, same size as the window.
    output: Target,
    chain: Option<FilterChain>,
    lines: u32,
    frame: usize,
    timer: GpuTimer,
}

impl Renderer {
    pub fn new(
        gl: Arc<glow::Context>,
        capture: (u32, u32),
        window: (u32, u32),
        lines: u32,
    ) -> Result<Self> {
        let desktop = Target::new(&gl, capture.0, capture.1)?;
        let output = Target::new(&gl, window.0, window.1)?;
        let timer = GpuTimer::new(&gl);
        let mut r = Renderer {
            gl,
            desktop,
            half: None,
            small: None,
            output,
            chain: None,
            lines,
            frame: 0,
            timer,
        };
        r.configure_input()?;
        Ok(r)
    }

    pub fn gl(&self) -> &glow::Context {
        &self.gl
    }

    /// Number of scanlines = height of the shader input.
    pub fn set_lines(&mut self, lines: u32) -> Result<()> {
        self.lines = lines.clamp(1, self.desktop.height);
        self.configure_input()
    }

    fn configure_input(&mut self) -> Result<()> {
        let (dw, dh) = (self.desktop.width, self.desktop.height);
        let h = self.lines.min(dh).max(1);
        let w = ((dw as u64 * h as u64 + dh as u64 / 2) / dh as u64).max(1) as u32;
        let want_half = h * 2 <= dh;
        resize(
            &self.gl,
            &mut self.half,
            want_half.then_some((dw.div_ceil(2), dh.div_ceil(2))),
        )?;
        resize(&self.gl, &mut self.small, Some((w, h)))?;
        log::info!("shader input {w}x{h} (desktop shrunk to {h} lines)");
        Ok(())
    }

    pub fn load_preset(&mut self, path: &Path, params: &BTreeMap<String, f32>) -> Result<()> {
        log::info!("loading preset {}", path.display());
        let preset = ShaderPreset::try_parse_with_driver_context(
            path,
            ShaderFeatures::ORIGINAL_ASPECT_UNIFORMS | ShaderFeatures::FRAMETIME_UNIFORMS,
            VideoDriver::GlCore,
        )
        .with_context(|| format!("parse {}", path.display()))?;
        let options = FilterChainOptions {
            glsl_version: 330,
            use_dsa: false,
            force_no_mipmaps: false,
            disable_cache: true,
        };
        let chain =
            unsafe { FilterChain::load_from_preset(preset, Arc::clone(&self.gl), Some(&options)) }
                .context("compile shader chain")?;
        for (name, value) in params {
            if chain
                .parameters()
                .set_parameter_value(name, *value)
                .is_none()
            {
                log::debug!("parameter '{name}' is not used by this preset");
            }
        }
        self.chain = Some(chain);
        self.frame = 0;
        self.set_lines(self.lines)
    }

    /// GPU time of the last finished frame, in milliseconds.
    pub fn gpu_ms(&self) -> Option<f64> {
        self.timer.last_ms
    }

    pub fn begin_frame(&mut self) {
        self.timer.begin(&self.gl);
    }

    /// Run the shader on the current desktop texture and draw it into the window
    /// (default framebuffer). Falls back to a plain copy when no chain is loaded.
    pub fn draw(&mut self, window_size: (u32, u32)) -> Result<()> {
        self.render_output(window_size)?;
        self.present();
        Ok(())
    }

    fn render_output(&mut self, window_size: (u32, u32)) -> Result<()> {
        let gl = &self.gl;

        // 1. Classic presets: shrink the desktop to `lines` rows.
        if let Some(small) = &self.small {
            let mut src = &self.desktop;
            for dst in [self.half.as_ref(), Some(small)].into_iter().flatten() {
                blit(gl, src, dst, glow::LINEAR, false);
                src = dst;
            }
        }
        let source = self.small.as_ref().unwrap_or(&self.desktop);

        if window_size != (self.output.width, self.output.height) {
            let out = Target::new(gl, window_size.0, window_size.1)?;
            self.output.destroy(gl);
            self.output = out;
        }

        // 2. Shader chain: source -> output.
        if let Some(chain) = self.chain.as_mut() {
            let out_image = self.output.image();
            let viewport = Viewport {
                x: 0.0,
                y: 0.0,
                mvp: None,
                output: &out_image,
                size: out_image.size,
            };
            let opts = FrameOptions::default();
            unsafe { chain.frame(&source.image(), &viewport, self.frame, Some(&opts)) }
                .context("shader frame")?;
        } else {
            blit(gl, source, &self.output, glow::NEAREST, false);
        }
        self.frame = self.frame.wrapping_add(1);

        Ok(())
    }

    /// Render the shader output into the internal output texture only.
    pub fn draw_offscreen(&mut self, size: (u32, u32)) -> Result<()> {
        self.render_output(size)
    }

    /// Output texture as RGBA rows, top row first.
    pub fn read_output(&self) -> Vec<u8> {
        let (w, h) = (self.output.width, self.output.height);
        let mut buf = vec![0u8; (w * h * 4) as usize];
        unsafe {
            self.gl
                .bind_framebuffer(glow::READ_FRAMEBUFFER, Some(self.output.fbo));
            self.gl.pixel_store_i32(glow::PACK_ALIGNMENT, 1);
            self.gl.read_pixels(
                0,
                0,
                w as i32,
                h as i32,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelPackData::Slice(Some(&mut buf)),
            );
            self.gl.bind_framebuffer(glow::READ_FRAMEBUFFER, None);
        }
        buf
    }

    fn present(&self) {
        let gl = &self.gl;
        // Flip rows-top-first texture into the bottom-up window.
        unsafe {
            gl.bind_framebuffer(glow::READ_FRAMEBUFFER, Some(self.output.fbo));
            gl.bind_framebuffer(glow::DRAW_FRAMEBUFFER, None);
            let (w, h) = (self.output.width as i32, self.output.height as i32);
            gl.blit_framebuffer(
                0,
                0,
                w,
                h,
                0,
                h,
                w,
                0,
                glow::COLOR_BUFFER_BIT,
                glow::NEAREST,
            );
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        }
    }

    /// Call after every `begin_frame`, whether the frame succeeded or not.
    pub fn end_frame(&mut self) {
        self.timer.end(&self.gl);
    }
}

impl Drop for Renderer {
    fn drop(&mut self) {
        self.chain = None;
        self.timer.destroy(&self.gl);
        for t in [self.half.take(), self.small.take()].into_iter().flatten() {
            t.destroy(&self.gl);
        }
        self.desktop.destroy(&self.gl);
        self.output.destroy(&self.gl);
    }
}

fn resize(gl: &glow::Context, slot: &mut Option<Target>, size: Option<(u32, u32)>) -> Result<()> {
    let current = slot.as_ref().map(|t| (t.width, t.height));
    if current == size {
        return Ok(());
    }
    if let Some(t) = slot.take() {
        t.destroy(gl);
    }
    if let Some((w, h)) = size {
        *slot = Some(Target::new(gl, w, h)?);
    }
    Ok(())
}

fn blit(gl: &glow::Context, src: &Target, dst: &Target, filter: u32, flip: bool) {
    unsafe {
        gl.bind_framebuffer(glow::READ_FRAMEBUFFER, Some(src.fbo));
        gl.bind_framebuffer(glow::DRAW_FRAMEBUFFER, Some(dst.fbo));
        let (w, h) = (dst.width as i32, dst.height as i32);
        let (y0, y1) = if flip { (h, 0) } else { (0, h) };
        gl.blit_framebuffer(
            0,
            0,
            src.width as i32,
            src.height as i32,
            0,
            y0,
            w,
            y1,
            glow::COLOR_BUFFER_BIT,
            filter,
        );
        gl.bind_framebuffer(glow::FRAMEBUFFER, None);
    }
}

/// GL_TIME_ELAPSED queries in a small ring, read back a few frames later so
/// measuring never stalls the pipeline.
struct GpuTimer {
    queries: Vec<glow::Query>,
    pending: Vec<bool>,
    next: usize,
    active: bool,
    last_ms: Option<f64>,
}

impl GpuTimer {
    fn new(gl: &glow::Context) -> GpuTimer {
        let queries: Vec<_> = (0..4)
            .filter_map(|_| unsafe { gl.create_query().ok() })
            .collect();
        let n = queries.len();
        GpuTimer {
            queries,
            pending: vec![false; n],
            next: 0,
            active: false,
            last_ms: None,
        }
    }

    fn begin(&mut self, gl: &glow::Context) {
        if self.queries.len() < 2 {
            return;
        }
        // Collect any finished results first.
        for i in 0..self.queries.len() {
            let q = self.queries[i];
            if self.pending[i]
                && unsafe { gl.get_query_parameter_u32(q, glow::QUERY_RESULT_AVAILABLE) } != 0
            {
                let ns = unsafe { gl.get_query_parameter_u32(q, glow::QUERY_RESULT) };
                self.last_ms = Some(ns as f64 / 1e6);
                self.pending[i] = false;
            }
        }
        if self.pending[self.next] {
            return; // GPU is far behind; skip measuring this frame.
        }
        unsafe { gl.begin_query(glow::TIME_ELAPSED, self.queries[self.next]) };
        self.active = true;
    }

    fn end(&mut self, gl: &glow::Context) {
        if !self.active {
            return;
        }
        unsafe { gl.end_query(glow::TIME_ELAPSED) };
        self.active = false;
        self.pending[self.next] = true;
        self.next = (self.next + 1) % self.queries.len();
    }

    fn destroy(&self, gl: &glow::Context) {
        for q in &self.queries {
            unsafe { gl.delete_query(*q) };
        }
    }
}
