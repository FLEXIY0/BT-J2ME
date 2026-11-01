//! `--render-still IN.png OUT.png WIDTH HEIGHT [--preset NAME] [--config FILE] [--frames N] [--renderer full|lite]`
//!
//! Runs one image through exactly the same GL path and shader chain as the
//! live overlay and saves the result. The input is treated as the shader
//! input at its native resolution (one image row = one scanline for classic
//! CRT presets). Used to compare the effect with reference pictures.

use crate::config::Config;
use anyhow::{Context, Result, anyhow, bail};
use std::path::PathBuf;
use winit::application::ApplicationHandler;
use winit::event::WindowEvent;
use winit::event_loop::{ActiveEventLoop, EventLoop};
use winit::window::{Window, WindowId};

pub struct Job {
    input: PathBuf,
    output: PathBuf,
    size: (u32, u32),
    preset: Option<String>,
    config: Option<PathBuf>,
    frames: usize,
    renderer: Option<String>,
}

impl Job {
    pub fn from_args() -> Result<Option<Job>> {
        let args: Vec<String> = std::env::args().collect();
        let Some(i) = args.iter().position(|a| a == "--render-still") else {
            return Ok(None);
        };
        let get = |k: usize| {
            args.get(i + k)
                .cloned()
                .ok_or_else(|| anyhow!("usage: --render-still IN.png OUT.png WIDTH HEIGHT"))
        };
        let opt = |name: &str| {
            args.iter()
                .position(|a| a == name)
                .and_then(|j| args.get(j + 1).cloned())
        };
        Ok(Some(Job {
            input: get(1)?.into(),
            output: get(2)?.into(),
            size: (get(3)?.parse()?, get(4)?.parse()?),
            preset: opt("--preset"),
            config: opt("--config").map(PathBuf::from),
            renderer: opt("--renderer"),
            frames: opt("--frames").map(|f| f.parse()).transpose()?.unwrap_or(4),
        }))
    }
}

pub fn run(job: Job) -> Result<()> {
    let event_loop = EventLoop::new()?;
    let mut app = StillApp { job, result: None };
    event_loop.run_app(&mut app)?;
    app.result.unwrap_or_else(|| Err(anyhow!("no result")))
}

struct StillApp {
    job: Job,
    result: Option<Result<()>>,
}

impl ApplicationHandler for StillApp {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.result.is_none() {
            self.result = Some(render(&self.job, event_loop));
            event_loop.exit();
        }
    }
    fn window_event(&mut self, _: &ActiveEventLoop, _: WindowId, _: WindowEvent) {}
}

fn render(job: &Job, event_loop: &ActiveEventLoop) -> Result<()> {
    let mut cfg = match &job.config {
        Some(p) => Config::load_or_create(p),
        None => Config::default(),
    };
    if let Some(p) = &job.preset {
        cfg.preset = p.clone();
    }
    let img = image::open(&job.input)
        .with_context(|| format!("open {}", job.input.display()))?
        .to_rgba8();
    let (iw, ih) = img.dimensions();
    if job.size.0 == 0 || job.size.1 == 0 {
        bail!("output size must be positive");
    }
    let attrs = Window::default_attributes()
        .with_visible(false)
        .with_inner_size(winit::dpi::PhysicalSize::new(64, 64));
    let mode = crate::Mode::parse(job.renderer.as_deref().unwrap_or(&cfg.renderer));
    let (_window, _surface, _context, gl, lite) = crate::create_gl(event_loop, attrs, false, mode)?;

    // One image row = one scanline: the "desktop" is the image itself.
    cfg.lines = ih;
    let mut renderer = crate::gfx::Gfx::new(gl.clone(), lite, (iw, ih), job.size, &cfg)?;
    upload_rgba(&gl, renderer.desktop(), img.as_raw(), iw, ih);
    for _ in 0..job.frames.max(1) {
        renderer.draw_offscreen(job.size)?;
    }
    let pixels = renderer.read_output();
    let out = image::RgbaImage::from_raw(job.size.0, job.size.1, pixels)
        .ok_or_else(|| anyhow!("bad output size"))?;
    out.save(&job.output)
        .with_context(|| format!("save {}", job.output.display()))?;
    log::info!(
        "rendered {} -> {} ({}x{})",
        job.input.display(),
        job.output.display(),
        job.size.0,
        job.size.1
    );
    Ok(())
}

fn upload_rgba(gl: &glow::Context, target: &crate::render::Target, data: &[u8], w: u32, h: u32) {
    use glow::HasContext;
    unsafe {
        gl.bind_texture(glow::TEXTURE_2D, Some(target.tex));
        gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 1);
        gl.tex_sub_image_2d(
            glow::TEXTURE_2D,
            0,
            0,
            0,
            w as i32,
            h as i32,
            glow::RGBA,
            glow::UNSIGNED_BYTE,
            glow::PixelUnpackData::Slice(Some(data)),
        );
        gl.bind_texture(glow::TEXTURE_2D, None);
    }
}
