//! The two renderers behind one interface: our own CRT Tube renderer
//! (OpenGL 2.1+, small) and librashader for any other .slangp preset
//! (OpenGL 3.3).

use crate::config::Config;
use crate::lite::TubeRenderer;
use crate::render::{Renderer, Target};
use anyhow::Result;
use std::sync::Arc;

pub enum Gfx {
    Full(Box<Renderer>),
    Tube(TubeRenderer),
}

impl Gfx {
    /// `compat`: the context is a compatibility one (created for the tube
    /// renderer, or because OpenGL 3.3 core is not available).
    pub fn new(
        gl: Arc<glow::Context>,
        compat: bool,
        capture: (u32, u32),
        window: (u32, u32),
        cfg: &Config,
    ) -> Result<Gfx> {
        let lines = cfg.effective_lines(capture.1);
        if compat {
            if cfg.preset != "crt-tube" {
                log::warn!(
                    "OpenGL 3.3 is not available: preset '{}' replaced by CRT Tube",
                    cfg.preset
                );
            }
            let v = glow::HasContext::version(&*gl);
            let lite = cfg.renderer == "lite" || (v.major, v.minor) < (3, 0);
            let mut r = TubeRenderer::new(gl, capture, lines, lite)?;
            r.set_params(&cfg.params);
            trim_heap();
            return Ok(Gfx::Tube(r));
        }
        let mut r = Renderer::new(gl, capture, window, lines)?;
        if let Err(e) = crate::load_preset(&mut r, cfg) {
            log::error!("{e:#}");
        }
        Ok(Gfx::Full(Box::new(r)))
    }

    /// Whether librashader is needed: any preset but crt-tube, or forced.
    pub fn needs_librashader(cfg: &Config) -> bool {
        cfg.renderer == "full" || cfg.preset != "crt-tube"
    }

    /// The 2-row tube renderer for weak GPUs (30 fps by default).
    pub fn is_lite(&self) -> bool {
        matches!(self, Gfx::Tube(r) if r.is_lite())
    }

    pub fn gl(&self) -> &glow::Context {
        match self {
            Gfx::Full(r) => r.gl(),
            Gfx::Tube(r) => r.gl(),
        }
    }

    pub fn desktop(&self) -> &Target {
        match self {
            Gfx::Full(r) => &r.desktop,
            Gfx::Tube(r) => &r.desktop,
        }
    }

    pub fn set_lines(&mut self, lines: u32) -> Result<()> {
        match self {
            Gfx::Full(r) => r.set_lines(lines),
            Gfx::Tube(r) => r.set_lines(lines),
        }
    }

    /// Reload shader / parameters from the config.
    pub fn reload(&mut self, cfg: &Config) -> Result<()> {
        match self {
            Gfx::Full(r) => crate::load_preset(r, cfg),
            Gfx::Tube(r) => {
                r.set_params(&cfg.params);
                Ok(())
            }
        }
    }

    pub fn begin_frame(&mut self) {
        if let Gfx::Full(r) = self {
            r.begin_frame();
        }
    }

    pub fn end_frame(&mut self) {
        if let Gfx::Full(r) = self {
            r.end_frame();
        }
    }

    pub fn draw(&mut self, size: (u32, u32)) -> Result<()> {
        match self {
            Gfx::Full(r) => r.draw(size),
            Gfx::Tube(r) => r.draw(size),
        }
    }

    pub fn draw_offscreen(&mut self, size: (u32, u32)) -> Result<()> {
        match self {
            Gfx::Full(r) => r.draw_offscreen(size),
            Gfx::Tube(r) => r.draw_offscreen(size),
        }
    }

    pub fn read_output(&self) -> Vec<u8> {
        match self {
            Gfx::Full(r) => r.read_output(),
            Gfx::Tube(r) => r.read_output(),
        }
    }

    pub fn gpu_ms(&self) -> Option<f64> {
        match self {
            Gfx::Full(r) => r.gpu_ms(),
            Gfx::Tube(_) => None,
        }
    }
}

/// Return freed heap (shader compiler garbage) to the system.
pub fn trim_heap() {
    #[cfg(all(target_os = "linux", target_env = "gnu"))]
    unsafe {
        libc::malloc_trim(0);
    }
}
