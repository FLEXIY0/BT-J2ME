//! The two renderers behind one interface: the full librashader path
//! (OpenGL 3.3, any .slangp preset) and the lite path (OpenGL 2.1, CRT Tube).

use crate::config::Config;
use crate::lite::LiteRenderer;
use crate::render::{Renderer, Target};
use anyhow::Result;
use std::sync::Arc;

pub enum Gfx {
    Full(Box<Renderer>),
    Lite(LiteRenderer),
}

impl Gfx {
    pub fn new(
        gl: Arc<glow::Context>,
        lite: bool,
        capture: (u32, u32),
        window: (u32, u32),
        cfg: &Config,
    ) -> Result<Gfx> {
        let lines = cfg.effective_lines(capture.1);
        if lite {
            if cfg.preset != "crt-tube" {
                log::warn!(
                    "lite renderer only draws CRT Tube; preset '{}' ignored",
                    cfg.preset
                );
            }
            let mut r = LiteRenderer::new(gl, capture, lines)?;
            r.set_params(&cfg.params);
            return Ok(Gfx::Lite(r));
        }
        let mut r = Renderer::new(gl, capture, window, lines)?;
        if let Err(e) = crate::load_preset(&mut r, cfg) {
            log::error!("{e:#}");
        }
        Ok(Gfx::Full(Box::new(r)))
    }

    pub fn is_lite(&self) -> bool {
        matches!(self, Gfx::Lite(_))
    }

    pub fn gl(&self) -> &glow::Context {
        match self {
            Gfx::Full(r) => r.gl(),
            Gfx::Lite(r) => r.gl(),
        }
    }

    pub fn desktop(&self) -> &Target {
        match self {
            Gfx::Full(r) => &r.desktop,
            Gfx::Lite(r) => &r.desktop,
        }
    }

    pub fn set_lines(&mut self, lines: u32) -> Result<()> {
        match self {
            Gfx::Full(r) => r.set_lines(lines),
            Gfx::Lite(r) => r.set_lines(lines),
        }
    }

    /// Reload shader / parameters from the config.
    pub fn reload(&mut self, cfg: &Config) -> Result<()> {
        match self {
            Gfx::Full(r) => crate::load_preset(r, cfg),
            Gfx::Lite(r) => {
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
            Gfx::Lite(r) => r.draw(size),
        }
    }

    pub fn draw_offscreen(&mut self, size: (u32, u32)) -> Result<()> {
        match self {
            Gfx::Full(r) => r.draw_offscreen(size),
            Gfx::Lite(r) => r.draw_offscreen(size),
        }
    }

    pub fn read_output(&self) -> Vec<u8> {
        match self {
            Gfx::Full(r) => r.read_output(),
            Gfx::Lite(r) => r.read_output(),
        }
    }

    pub fn gpu_ms(&self) -> Option<f64> {
        match self {
            Gfx::Full(r) => r.gpu_ms(),
            Gfx::Lite(_) => None,
        }
    }
}
