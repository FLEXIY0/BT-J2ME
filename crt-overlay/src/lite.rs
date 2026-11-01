//! "Lite" renderer for old GPUs that only have OpenGL 2.1 (Intel GMA 4500,
//! GMA 3150, old Radeon/GeForce...). It does not use librashader; it draws
//! the CRT Tube look with three small GLSL 1.20 passes:
//!
//! 1. shrink the desktop to `lines` rows (4x4 box filter in 4 bilinear taps);
//! 2. at that low resolution, convert to YIQ and blur the colour (I/Q)
//!    horizontally — the expensive wide filter runs on ~100k pixels only;
//! 3. at screen resolution: luma beam (6 taps x 2 rows), scanlines whose
//!    width depends on brightness, aperture grille, colour controls.
//!
//! About 14 texture reads per screen pixel instead of 40. The parameter
//! defaults are read from crt-tube.slang, so both renderers share one source
//! of truth and the same `[params]` overrides.

use crate::glutil::{Quad, program};
use crate::render::Target;
use anyhow::{Context, Result};
use glow::HasContext;
use std::collections::{BTreeMap, HashMap};
use std::sync::Arc;

const TUBE_SLANG: &str = include_str!("../shaders/shaders/tube/crt-tube.slang");

const VS: &str = r#"#version 120
attribute vec2 pos;
varying vec2 uv;
uniform float flip;
void main() {
    uv = vec2(pos.x, flip > 0.5 ? 1.0 - pos.y : pos.y);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}"#;

const DOWN_FS: &str = r#"#version 120
varying vec2 uv;
uniform sampler2D tex;
uniform vec2 off;   // a quarter of a destination texel, in uv units
void main() {
    vec3 c = texture2D(tex, uv + vec2(-off.x, -off.y)).rgb
           + texture2D(tex, uv + vec2( off.x, -off.y)).rgb
           + texture2D(tex, uv + vec2(-off.x,  off.y)).rgb
           + texture2D(tex, uv + vec2( off.x,  off.y)).rgb;
    gl_FragColor = vec4(c * 0.25, 1.0);
}"#;

const PREP_FS: &str = r#"#version 120
varying vec2 uv;
uniform sampler2D tex;
uniform vec2 size;
uniform float chroma;
void main() {
    vec2 t = floor(uv * size);
    vec3 c0 = texture2D(tex, (t + 0.5) / size).rgb;
    float Y = dot(c0, vec3(0.299, 0.587, 0.114));
    vec2 iq = vec2(0.0);
    float w = 0.0;
    float s = max(chroma, 0.05);
    for (int i = -5; i <= 5; i++) {
        float x = clamp(t.x + float(i), 0.0, size.x - 1.0);
        vec3 c = texture2D(tex, vec2(x + 0.5, t.y + 0.5) / size).rgb;
        float k = exp(-0.5 * float(i * i) / (s * s));
        iq += k * vec2(dot(c, vec3(0.596, -0.274, -0.322)), dot(c, vec3(0.211, -0.523, 0.312)));
        w += k;
    }
    iq /= w;
    gl_FragColor = vec4(Y, iq.x / 1.2 + 0.5, iq.y / 1.1 + 0.5, 1.0);
}"#;

const TUBE_FS: &str = r#"#version 120
varying vec2 uv;
uniform sampler2D yiq;
uniform vec2 size;
uniform vec2 outsize;
uniform float TUBE_HBEAM, TUBE_WMIN, TUBE_WMAX, TUBE_MASK_PITCH, TUBE_MASK_STR,
              TUBE_CONV_R, TUBE_CONV_B, TUBE_BRIGHT, TUBE_GAIN_R, TUBE_GAIN_B,
              TUBE_GAMMA_IN, TUBE_GAMMA_OUT, TUBE_SAT, TUBE_MASK_PHASE, TUBE_COLOR;

vec3 yiq2rgb(vec3 v) {
    return vec3(v.x + 0.956 * v.y + 0.621 * v.z,
                v.x - 0.272 * v.y - 0.647 * v.z,
                v.x - 1.106 * v.y + 1.703 * v.z);
}

void main() {
    vec2 src = uv * size;
    vec2 px = uv * outsize;
    vec2 scale = outsize / size;
    vec3 shift = vec3(TUBE_CONV_R, 0.0, TUBE_CONV_B);
    float hw = max(TUBE_HBEAM, 0.05);
    float row0 = floor(src.y - 0.5);
    float col0 = floor(src.x - 0.5);
    vec3 sum = vec3(0.0);
    for (int dj = 0; dj <= 1; dj++) {
        float row = row0 + float(dj);
        float rowc = clamp(row, 0.0, size.y - 1.0);
        vec3 ys = vec3(0.0);
        vec3 yw = vec3(0.0);
        for (int di = -2; di <= 3; di++) {
            float col = col0 + float(di);
            float Y = texture2D(yiq, vec2(clamp(col, 0.0, size.x - 1.0) + 0.5, rowc + 0.5) / size).r;
            vec3 d = (src.x - shift - (col + 0.5)) / hw;
            vec3 k = exp(-0.5 * d * d);
            ys += k * Y;
            yw += k;
        }
        vec3 Yc = ys / max(yw, vec3(1e-6));
        vec2 iq = (texture2D(yiq, vec2(src.x, rowc + 0.5) / size).gb - 0.5) * vec2(1.2, 1.1) * TUBE_COLOR;
        vec3 h = vec3(yiq2rgb(vec3(Yc.r, iq)).r, yiq2rgb(vec3(Yc.g, iq)).g, yiq2rgb(vec3(Yc.b, iq)).b);
        h = pow(clamp(h, 0.0, 1.0), vec3(TUBE_GAMMA_IN));
        float dy = src.y - (row + 0.5);
        vec3 w = max(TUBE_WMIN + (TUBE_WMAX - TUBE_WMIN) * sqrt(h), vec3(0.03));
        sum += h * exp(-0.5 * dy * dy / (w * w)) / (w * 2.5066283);
    }
    float pitch = max(TUBE_MASK_PITCH * scale.y, 1.0);
    float ph = 6.2831853 * (px.x - TUBE_MASK_PHASE) / pitch;
    vec3 mask = 1.0 + TUBE_MASK_STR * cos(ph - vec3(0.0, 2.0943951, 4.1887902));
    vec3 col = sum * mask * TUBE_BRIGHT * vec3(TUBE_GAIN_R, 1.0, TUBE_GAIN_B);
    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = max(vec3(l) + (col - vec3(l)) * TUBE_SAT, vec3(0.0));
    gl_FragColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / TUBE_GAMMA_OUT)), 1.0);
}"#;

const UNIFORMS: &[&str] = &[
    "TUBE_HBEAM",
    "TUBE_WMIN",
    "TUBE_WMAX",
    "TUBE_MASK_PITCH",
    "TUBE_MASK_STR",
    "TUBE_CONV_R",
    "TUBE_CONV_B",
    "TUBE_BRIGHT",
    "TUBE_GAIN_R",
    "TUBE_GAIN_B",
    "TUBE_GAMMA_IN",
    "TUBE_GAMMA_OUT",
    "TUBE_SAT",
    "TUBE_MASK_PHASE",
    "TUBE_COLOR",
    "TUBE_CHROMA",
];

/// Defaults from the `#pragma parameter` lines of crt-tube.slang.
fn slang_defaults() -> HashMap<String, f32> {
    TUBE_SLANG
        .lines()
        .filter_map(|l| l.strip_prefix("#pragma parameter "))
        .filter_map(|rest| {
            let name = rest.split_whitespace().next()?;
            let after = rest.rsplit('"').next()?;
            let value = after.split_whitespace().next()?.parse().ok()?;
            Some((name.to_string(), value))
        })
        .collect()
}

pub struct LiteRenderer {
    gl: Arc<glow::Context>,
    pub desktop: Target,
    half: Option<Target>,
    small: Target,
    yiq: Target,
    output: Option<Target>,
    quad: Quad,
    down: glow::Program,
    prep: glow::Program,
    tube: glow::Program,
    params: HashMap<String, f32>,
    lines: u32,
}

impl LiteRenderer {
    pub fn new(gl: Arc<glow::Context>, capture: (u32, u32), lines: u32) -> Result<Self> {
        let down = program(&gl, VS, DOWN_FS).context("lite shrink shader")?;
        let prep = program(&gl, VS, PREP_FS).context("lite colour shader")?;
        let tube = program(&gl, VS, TUBE_FS).context("lite tube shader")?;
        let quad = Quad::new(&gl)?;
        let desktop = Target::new(&gl, capture.0, capture.1)?;
        let small = Target::new(&gl, 1, 1)?;
        let yiq = Target::new(&gl, 1, 1)?;
        let mut r = LiteRenderer {
            gl,
            desktop,
            half: None,
            small,
            yiq,
            output: None,
            quad,
            down,
            prep,
            tube,
            params: slang_defaults(),
            lines,
        };
        r.set_lines(lines)?;
        log::info!("lite renderer (OpenGL 2.1 path)");
        Ok(r)
    }

    pub fn gl(&self) -> &glow::Context {
        &self.gl
    }

    pub fn set_params(&mut self, overrides: &BTreeMap<String, f32>) {
        self.params = slang_defaults();
        for (k, v) in overrides {
            if UNIFORMS.contains(&k.as_str()) {
                self.params.insert(k.clone(), *v);
            }
        }
    }

    pub fn set_lines(&mut self, lines: u32) -> Result<()> {
        let (dw, dh) = (self.desktop.width, self.desktop.height);
        self.lines = lines.clamp(1, dh);
        let h = self.lines;
        let w = ((dw as u64 * h as u64 + dh as u64 / 2) / dh as u64).max(1) as u32;
        let gl = &self.gl;
        // Shrinking by more than 4x in one 4x4 box pass would skip pixels.
        let want_half = dh as f32 / h as f32 > 4.0;
        if let Some(t) = self.half.take() {
            t.destroy(gl);
        }
        if want_half {
            self.half = Some(Target::new(gl, dw.div_ceil(2), dh.div_ceil(2))?);
        }
        let small = Target::new(gl, w, h)?;
        let yiq = Target::new(gl, w, h)?;
        std::mem::replace(&mut self.small, small).destroy(gl);
        std::mem::replace(&mut self.yiq, yiq).destroy(gl);
        log::info!("lite: shader input {w}x{h}");
        Ok(())
    }

    fn pass(
        &self,
        prog: glow::Program,
        src: &Target,
        dst: Option<&Target>,
        viewport: (u32, u32),
        flip: bool,
        setup: impl Fn(&glow::Context),
    ) {
        let gl = &self.gl;
        unsafe {
            gl.bind_framebuffer(glow::FRAMEBUFFER, dst.map(|t| t.fbo));
            gl.viewport(0, 0, viewport.0 as i32, viewport.1 as i32);
            gl.disable(glow::BLEND);
            gl.use_program(Some(prog));
            gl.active_texture(glow::TEXTURE0);
            gl.bind_texture(glow::TEXTURE_2D, Some(src.tex));
            gl.uniform_1_i32(gl.get_uniform_location(prog, "tex").as_ref(), 0);
            gl.uniform_1_i32(gl.get_uniform_location(prog, "yiq").as_ref(), 0);
            gl.uniform_1_f32(
                gl.get_uniform_location(prog, "flip").as_ref(),
                if flip { 1.0 } else { 0.0 },
            );
            setup(gl);
            self.quad.draw(gl);
            gl.use_program(None);
            gl.bind_texture(glow::TEXTURE_2D, None);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        }
    }

    fn render(&mut self, to_screen: bool, size: (u32, u32)) -> Result<()> {
        // 1. Shrink.
        let mut src = &self.desktop;
        for dst in [self.half.as_ref(), Some(&self.small)]
            .into_iter()
            .flatten()
        {
            let off = (0.25 / dst.width as f32, 0.25 / dst.height as f32);
            self.pass(
                self.down,
                src,
                Some(dst),
                (dst.width, dst.height),
                false,
                |gl| unsafe {
                    gl.uniform_2_f32(
                        gl.get_uniform_location(self.down, "off").as_ref(),
                        off.0,
                        off.1,
                    );
                },
            );
            src = dst;
        }
        // 2. YIQ with smeared colour, at low resolution.
        let size_s = (self.small.width as f32, self.small.height as f32);
        let chroma = self.params.get("TUBE_CHROMA").copied().unwrap_or(1.9);
        self.pass(
            self.prep,
            &self.small,
            Some(&self.yiq),
            (self.small.width, self.small.height),
            false,
            |gl| unsafe {
                gl.uniform_2_f32(
                    gl.get_uniform_location(self.prep, "size").as_ref(),
                    size_s.0,
                    size_s.1,
                );
                gl.uniform_1_f32(
                    gl.get_uniform_location(self.prep, "chroma").as_ref(),
                    chroma,
                );
            },
        );
        // 3. The tube, at screen resolution.
        if !to_screen {
            let reuse = matches!(&self.output, Some(t) if (t.width, t.height) == size);
            if !reuse {
                if let Some(t) = self.output.take() {
                    t.destroy(&self.gl);
                }
                self.output = Some(Target::new(&self.gl, size.0, size.1)?);
            }
        }
        let dst = if to_screen {
            None
        } else {
            self.output.as_ref()
        };
        self.pass(self.tube, &self.yiq, dst, size, to_screen, |gl| unsafe {
            gl.uniform_2_f32(
                gl.get_uniform_location(self.tube, "size").as_ref(),
                size_s.0,
                size_s.1,
            );
            gl.uniform_2_f32(
                gl.get_uniform_location(self.tube, "outsize").as_ref(),
                size.0 as f32,
                size.1 as f32,
            );
            for name in UNIFORMS {
                if let Some(v) = self.params.get(*name) {
                    gl.uniform_1_f32(gl.get_uniform_location(self.tube, name).as_ref(), *v);
                }
            }
        });
        Ok(())
    }

    /// Draw into the window (default framebuffer).
    pub fn draw(&mut self, window_size: (u32, u32)) -> Result<()> {
        self.render(true, window_size)
    }

    pub fn draw_offscreen(&mut self, size: (u32, u32)) -> Result<()> {
        self.render(false, size)
    }

    pub fn read_output(&self) -> Vec<u8> {
        let Some(t) = &self.output else {
            return Vec::new();
        };
        let mut buf = vec![0u8; (t.width * t.height * 4) as usize];
        unsafe {
            self.gl
                .bind_framebuffer(glow::READ_FRAMEBUFFER, Some(t.fbo));
            self.gl.pixel_store_i32(glow::PACK_ALIGNMENT, 1);
            self.gl.read_pixels(
                0,
                0,
                t.width as i32,
                t.height as i32,
                glow::RGBA,
                glow::UNSIGNED_BYTE,
                glow::PixelPackData::Slice(Some(&mut buf)),
            );
            self.gl.bind_framebuffer(glow::READ_FRAMEBUFFER, None);
        }
        buf
    }
}

impl Drop for LiteRenderer {
    fn drop(&mut self) {
        let gl = &self.gl;
        for t in [
            Some(&self.desktop),
            self.half.as_ref(),
            Some(&self.small),
            Some(&self.yiq),
            self.output.as_ref(),
        ]
        .into_iter()
        .flatten()
        {
            t.destroy(gl);
        }
        self.quad.destroy(gl);
        unsafe {
            gl.delete_program(self.down);
            gl.delete_program(self.prep);
            gl.delete_program(self.tube);
        }
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn defaults_come_from_the_slang_file() {
        let d = super::slang_defaults();
        for name in super::UNIFORMS {
            assert!(d.contains_key(*name), "{name}");
        }
    }
}
