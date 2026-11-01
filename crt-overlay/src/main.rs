//! CRT Overlay: runs the CRT-Guest-Advanced shader over the whole desktop.
//!
//! A borderless, always-on-top, input-transparent window covers one monitor.
//! Every frame the desktop *under* it is captured, shrunk to a "CRT
//! resolution" and pushed through the RetroArch slang shader chain (via
//! librashader). The window cannot be focused or closed; it is controlled
//! only from the tray icon.

#![cfg_attr(all(windows, not(debug_assertions)), windows_subsystem = "windows")]

mod capture;
mod config;
mod gfx;
mod glutil;
mod lite;
mod render;
mod shaders;
mod stats;
mod still;
mod tray;

use anyhow::{Context, Result, anyhow, bail};
use capture::{Capture, Rect};
use config::Config;
use glutin::config::{ConfigTemplateBuilder, GlConfig};
use glutin::context::{
    ContextApi, ContextAttributesBuilder, GlProfile, NotCurrentGlContext, PossiblyCurrentContext,
    Version,
};
use glutin::display::{GetGlDisplay, GlDisplay};
use glutin::surface::{GlSurface, Surface, SwapInterval, WindowSurface};
use glutin_winit::{DisplayBuilder, GlWindow};
use raw_window_handle::{HasWindowHandle, RawWindowHandle};
use render::Renderer;
use std::num::NonZeroU32;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tray::{Command, Tray};
use winit::application::ApplicationHandler;
use winit::event::{StartCause, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::monitor::MonitorHandle;
use winit::window::{Window, WindowId, WindowLevel};

#[derive(Debug)]
pub enum UserEvent {
    Menu(tray_icon::menu::MenuEvent),
}

/// Everything that lives only while the shader is on.
struct Overlay {
    // Field order = drop order: GL objects before the context, window last.
    renderer: gfx::Gfx,
    capture: Box<dyn Capture>,
    surface: Surface<WindowSurface>,
    context: PossiblyCurrentContext,
    window: Window,
    shown: bool,
    /// At least one real desktop image has been captured.
    got_frame: bool,
    created: Instant,
    refresh_hz: f64,
    failures: u32,
}

struct App {
    config_path: PathBuf,
    cfg: Config,
    tray: Option<Tray>,
    overlay: Option<Overlay>,
    next_frame: Instant,
    stats: stats::Stats,
}

fn main() {
    // One malloc arena per helper thread (tray D-Bus, X11) wastes memory;
    // the program allocates almost nothing after start-up.
    #[cfg(all(target_os = "linux", target_env = "gnu"))]
    unsafe {
        libc::mallopt(libc::M_ARENA_MAX, 1);
    }
    init_logging();
    if std::env::args().any(|a| a == "--render-still") {
        if let Err(e) = run() {
            eprintln!("crt-overlay: {e:#}");
            std::process::exit(1);
        }
        return;
    }
    let Some(_instance) = single_instance() else {
        log::info!("already running");
        show_error(
            "CRT Overlay is already running.\n\nUse its icon in the system tray (it may be hidden under the ^ arrow).",
        );
        return;
    };
    if let Err(e) = run() {
        log::error!("{e:#}");
        eprintln!("crt-overlay: {e:#}");
        show_error(&format!("{e:#}"));
        std::process::exit(1);
    }
}

/// Returns a guard while this is the only running copy, `None` otherwise.
#[cfg(windows)]
fn single_instance() -> Option<impl Sized> {
    use windows::Win32::Foundation::{ERROR_ALREADY_EXISTS, GetLastError};
    use windows::Win32::System::Threading::CreateMutexW;
    use windows::core::w;
    let handle =
        unsafe { CreateMutexW(None, true, w!("Local\\crt-overlay-single-instance")) }.ok()?;
    if unsafe { GetLastError() } == ERROR_ALREADY_EXISTS {
        return None;
    }
    Some(handle)
}

#[cfg(target_os = "linux")]
fn single_instance() -> Option<impl Sized> {
    use std::os::linux::net::SocketAddrExt;
    use std::os::unix::net::{SocketAddr, UnixListener};
    // Abstract socket: released automatically when the process exits.
    let name = format!("crt-overlay-{}", unsafe { libc::getuid() });
    let addr = SocketAddr::from_abstract_name(name.as_bytes()).ok()?;
    UnixListener::bind_addr(&addr).ok()
}

fn run() -> Result<()> {
    if let Some(job) = still::Job::from_args()? {
        return still::run(job);
    }
    let config_path = std::env::args()
        .skip_while(|a| a != "--config")
        .nth(1)
        .map(PathBuf::from)
        .unwrap_or_else(Config::default_path);
    let cfg = Config::load_or_create(&config_path);
    log::info!("config: {}", config_path.display());

    let event_loop = build_event_loop()?;
    tray::forward_events(event_loop.create_proxy());
    let mut app = App {
        config_path,
        cfg,
        tray: None,
        overlay: None,
        next_frame: Instant::now(),
        stats: stats::Stats::new(),
    };
    event_loop.run_app(&mut app)?;
    Ok(())
}

#[cfg(target_os = "linux")]
fn build_event_loop() -> Result<EventLoop<UserEvent>> {
    use winit::platform::x11::EventLoopBuilderExtX11;
    if std::env::var_os("DISPLAY").is_none() {
        bail!(
            "no X11 display (DISPLAY is not set). CRT Overlay needs an X11 session; pure Wayland is not supported"
        );
    }
    if std::env::var_os("WAYLAND_DISPLAY").is_some() {
        log::warn!(
            "Wayland session detected: running through XWayland, only X11 windows can be captured"
        );
    }
    Ok(EventLoop::<UserEvent>::with_user_event()
        .with_x11()
        .build()?)
}

#[cfg(not(target_os = "linux"))]
fn build_event_loop() -> Result<EventLoop<UserEvent>> {
    Ok(EventLoop::<UserEvent>::with_user_event().build()?)
}

fn init_logging() {
    use std::io::IsTerminal;
    let mut b = env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info"));
    if !std::io::stderr().is_terminal() {
        // Started from a menu/autostart (or a GUI build on Windows): log to a file.
        let dir = dirs::cache_dir()
            .unwrap_or_else(std::env::temp_dir)
            .join("crt-overlay");
        let _ = std::fs::create_dir_all(&dir);
        if let Ok(f) = std::fs::File::create(dir.join("crt-overlay.log")) {
            b.target(env_logger::Target::Pipe(Box::new(f)));
        }
    }
    b.init();
}

impl ApplicationHandler<UserEvent> for App {
    fn new_events(&mut self, event_loop: &ActiveEventLoop, cause: StartCause) {
        if cause == StartCause::Init {
            let names: Vec<String> = event_loop
                .available_monitors()
                .map(|m| monitor_label(&m))
                .collect();
            match Tray::new(&self.cfg, &names) {
                Ok(t) => self.tray = Some(t),
                Err(e) => {
                    // Without a tray there would be no way to turn the overlay off.
                    log::error!("cannot create tray icon: {e:#}");
                    show_error(&format!(
                        "Cannot create the tray icon, so the overlay could not be turned off. Not starting.\n\n{e:#}"
                    ));
                    event_loop.exit();
                    return;
                }
            }
            if self.cfg.enabled {
                self.start(event_loop);
            }
        }
    }

    fn resumed(&mut self, _event_loop: &ActiveEventLoop) {}

    fn user_event(&mut self, event_loop: &ActiveEventLoop, event: UserEvent) {
        let UserEvent::Menu(e) = event;
        let Some(cmd) = self.tray.as_ref().and_then(|t| t.handle(&e)) else {
            return;
        };
        log::info!("tray: {cmd:?}");
        match cmd {
            Command::ToggleEnabled => {
                if self.overlay.is_some() {
                    self.overlay = None;
                } else {
                    self.start(event_loop);
                }
                self.cfg.enabled = self.overlay.is_some();
                if let Some(t) = &self.tray {
                    t.set_enabled(self.cfg.enabled);
                }
                self.save("enabled", self.cfg.enabled.into());
            }
            Command::SetPreset(p) => {
                self.cfg.preset = p.clone();
                self.save("preset", p.into());
                self.reload_shader();
            }
            Command::SetLines(n) => {
                self.cfg.lines = n;
                self.save("lines", (n as i64).into());
                if let Some(o) = self.overlay.as_mut() {
                    let lines = self.cfg.effective_lines(o.renderer.desktop().height);
                    if let Err(e) = o.renderer.set_lines(lines) {
                        log::error!("{e:#}");
                    }
                }
            }
            Command::SetMonitor(m) => {
                self.cfg.monitor = m;
                self.save("monitor", (m as i64).into());
                self.restart(event_loop);
            }
            Command::Reload => {
                self.cfg = Config::load_or_create(&self.config_path);
                self.restart(event_loop);
            }
            Command::OpenConfig => open_in_editor(&self.config_path),
            Command::Quit => {
                self.overlay = None;
                event_loop.exit();
            }
        }
    }

    fn window_event(&mut self, _event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        match event {
            // Closing is only possible from the tray.
            WindowEvent::CloseRequested => log::info!("close request ignored; use the tray icon"),
            WindowEvent::Resized(size) => {
                if let (Some(o), Some(w), Some(h)) = (
                    &self.overlay,
                    NonZeroU32::new(size.width),
                    NonZeroU32::new(size.height),
                ) {
                    o.surface.resize(&o.context, w, h);
                }
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, event_loop: &ActiveEventLoop) {
        let Some(o) = &self.overlay else {
            event_loop.set_control_flow(ControlFlow::Wait);
            return;
        };
        // Frames are drawn from here, not from RedrawRequested: Windows sends
        // no WM_PAINT to a hidden window, and the overlay stays hidden until
        // its first captured frame is ready.
        // With vsync (max_fps = 0) swap_buffers paces the loop; the limiter at
        // refresh rate + 5% only matters when vsync does not block.
        // Lite tube renderer = old hardware: 30 fps unless configured otherwise.
        let fps = if self.cfg.max_fps == 0 && o.renderer.is_lite() {
            30.0
        } else if self.cfg.max_fps > 0 {
            self.cfg.max_fps as f64
        } else {
            o.refresh_hz * 1.05
        };
        let now = Instant::now();
        if now >= self.next_frame {
            let period = Duration::from_secs_f64(1.0 / fps);
            self.next_frame = (self.next_frame + period).max(now);
            self.draw_frame();
        }
        if self.overlay.is_some() {
            event_loop.set_control_flow(ControlFlow::WaitUntil(self.next_frame));
        }
    }
}

impl App {
    fn save(&self, key: &str, value: toml_edit::Value) {
        if let Err(e) = config::save_value(&self.config_path, key, value) {
            log::warn!("cannot save setting {key}: {e:#}");
        }
    }

    fn start(&mut self, event_loop: &ActiveEventLoop) {
        match self.create_overlay(event_loop) {
            Ok(o) => self.overlay = Some(o),
            Err(e) => {
                log::error!("cannot start overlay: {e:#}");
                show_error(&format!("Cannot start the CRT overlay:\n\n{e:#}"));
                self.overlay = None;
            }
        }
        if let Some(t) = &self.tray {
            t.set_enabled(self.overlay.is_some());
        }
    }

    fn restart(&mut self, event_loop: &ActiveEventLoop) {
        if self.overlay.take().is_some() {
            self.start(event_loop);
        }
    }

    fn reload_shader(&mut self) {
        let Some(o) = self.overlay.as_mut() else {
            return;
        };
        if let Err(e) = o.renderer.reload(&self.cfg) {
            log::error!("{e:#}");
        }
    }

    fn draw_frame(&mut self) {
        let Some(o) = self.overlay.as_mut() else {
            return;
        };
        let size = o.window.inner_size();
        let mut capture_time = Duration::ZERO;
        o.renderer.begin_frame();
        let result = (|| -> Result<()> {
            let t = Instant::now();
            if o.capture.grab(o.renderer.gl(), o.renderer.desktop())? {
                o.got_frame = true;
            }
            capture_time = t.elapsed();
            o.renderer.draw((size.width.max(1), size.height.max(1)))?;
            Ok(())
        })();
        o.renderer.end_frame();
        let result = result.and_then(|()| Ok(o.surface.swap_buffers(&o.context)?));
        // Never leave the user with an invisible overlay that silently does
        // nothing, or with a black screen: give up loudly instead.
        let fatal = match &result {
            Err(e) => {
                o.failures += 1;
                log::error!("frame failed: {e:#}");
                (o.failures >= 30).then(|| format!("{e:#}"))
            }
            Ok(()) => {
                o.failures = 0;
                (!o.got_frame && o.created.elapsed() > Duration::from_secs(5)).then(|| {
                    "the screen capture did not deliver any image within 5 seconds".to_string()
                })
            }
        };
        if let Some(msg) = fatal {
            self.overlay = None;
            if let Some(t) = &self.tray {
                t.set_enabled(false);
            }
            log::error!("overlay stopped: {msg}");
            show_error(&format!("The CRT overlay stopped:\n\n{msg}"));
            return;
        }
        if result.is_err() || !o.got_frame {
            return;
        }
        if !o.shown {
            // Show only once there is a real frame, to avoid a black flash.
            o.window.set_visible(true);
            if let Err(e) = o.window.set_cursor_hittest(false) {
                log::warn!("cannot make the overlay click-through: {e}");
            }
            #[cfg(windows)]
            if let Ok(RawWindowHandle::Win32(h)) = o.window.window_handle().map(|h| h.as_raw()) {
                if let Err(e) = capture::windows::prepare_overlay_window(h.hwnd.get()) {
                    log::error!("{e:#}");
                }
            }
            o.shown = true;
        }
        self.stats.frame(capture_time, o.renderer.gpu_ms());
        if let (Some(r), Some(t)) = (self.stats.report(), &self.tray) {
            t.set_stats(&r.short());
        }
    }

    fn create_overlay(&mut self, event_loop: &ActiveEventLoop) -> Result<Overlay> {
        let monitor =
            pick_monitor(event_loop, self.cfg.monitor).ok_or_else(|| anyhow!("no monitors"))?;
        let pos = monitor.position();
        let size = monitor.size();
        let refresh_hz = monitor
            .refresh_rate_millihertz()
            .map(|m| m as f64 / 1000.0)
            .filter(|hz| *hz >= 20.0)
            .unwrap_or(60.0);
        let rect = Rect {
            x: pos.x,
            y: pos.y,
            w: size.width,
            h: size.height,
        };
        log::info!("overlay on {} at {rect:?}", monitor_label(&monitor));

        let vsync = self.cfg.max_fps == 0;
        let (window, surface, context, gl, lite) = create_gl(
            event_loop,
            window_attributes(pos, size),
            vsync,
            Mode::for_config(&self.cfg),
        )?;
        let raw = window.window_handle()?.as_raw();

        let capture = create_capture(raw, rect)?;
        let win_size = window.inner_size();
        let renderer = gfx::Gfx::new(
            gl,
            lite,
            (rect.w, rect.h),
            (win_size.width.max(1), win_size.height.max(1)),
            &self.cfg,
        )?;
        self.next_frame = Instant::now();
        Ok(Overlay {
            renderer,
            capture,
            surface,
            context,
            window,
            shown: false,
            got_frame: false,
            created: Instant::now(),
            refresh_hz,
            failures: 0,
        })
    }
}

/// Create a window with a current OpenGL 3.3 core context.
/// Which GL context to create. `Core`: OpenGL 3.3 core for librashader,
/// falling back to a compatibility context. `Compat`: a compatibility
/// context for the own CRT Tube renderer (works from OpenGL 2.1 up).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Mode {
    Core,
    Compat,
}

impl Mode {
    pub fn for_config(cfg: &Config) -> Mode {
        if gfx::Gfx::needs_librashader(cfg) {
            Mode::Core
        } else {
            Mode::Compat
        }
    }
}

/// Window, its surface, the current context, the GL function table and
/// whether the lite (OpenGL 2.1) renderer has to be used.
pub type GlWindowParts = (
    Window,
    Surface<WindowSurface>,
    PossiblyCurrentContext,
    Arc<glow::Context>,
    bool,
);

/// Create a window with a current OpenGL context. Returns `lite = true`
/// when only an OpenGL 2.1 context is used.
pub fn create_gl(
    event_loop: &ActiveEventLoop,
    attrs: winit::window::WindowAttributes,
    vsync: bool,
    mode: Mode,
) -> Result<GlWindowParts> {
    let template = ConfigTemplateBuilder::new().with_transparency(false);
    let (window, gl_config) = DisplayBuilder::new()
        .with_window_attributes(Some(attrs))
        .build(event_loop, template, |configs| {
            configs
                .reduce(|a, b| {
                    if b.num_samples() < a.num_samples() {
                        b
                    } else {
                        a
                    }
                })
                .expect("at least one GL config")
        })
        .map_err(|e| anyhow!("create window: {e}"))?;
    let window = window.ok_or_else(|| anyhow!("no window"))?;
    let display = gl_config.display();
    let raw = window.window_handle()?.as_raw();
    let core33 = ContextAttributesBuilder::new()
        .with_context_api(ContextApi::OpenGl(Some(Version::new(3, 3))))
        .with_profile(GlProfile::Core)
        .build(Some(raw));
    let compat21 = ContextAttributesBuilder::new()
        .with_context_api(ContextApi::OpenGl(Some(Version::new(2, 1))))
        .with_profile(GlProfile::Compatibility)
        .build(Some(raw));
    let (context, lite) = match mode {
        // Own tube renderer: a compatibility context; drivers hand out the
        // highest version they support (2.1 on GMA 4500, 4.x elsewhere).
        Mode::Compat => (
            unsafe { display.create_context(&gl_config, &compat21) }
                .context("create OpenGL context")?,
            true,
        ),
        Mode::Core => match unsafe { display.create_context(&gl_config, &core33) } {
            Ok(c) => (c, false),
            Err(e) => {
                log::warn!(
                    "OpenGL 3.3 is not available ({e}); falling back to CRT Tube on OpenGL 2.1"
                );
                (
                    unsafe { display.create_context(&gl_config, &compat21) }
                        .context("neither OpenGL 3.3 nor OpenGL 2.1 is available")?,
                    true,
                )
            }
        },
    };
    let surface_attrs = window.build_surface_attributes(Default::default())?;
    let surface = unsafe { display.create_window_surface(&gl_config, &surface_attrs) }?;
    let context = context.make_current(&surface)?;
    let interval = if vsync {
        SwapInterval::Wait(NonZeroU32::MIN)
    } else {
        SwapInterval::DontWait
    };
    if let Err(e) = surface.set_swap_interval(&context, interval) {
        log::warn!("cannot set swap interval: {e}");
    }
    let gl = Arc::new(unsafe {
        glow::Context::from_loader_function_cstr(|s| display.get_proc_address(s))
    });
    {
        use glow::HasContext;
        let (v, r) = unsafe {
            (
                gl.get_parameter_string(glow::VERSION),
                gl.get_parameter_string(glow::RENDERER),
            )
        };
        log::info!(
            "OpenGL {v} on {r}{}",
            if lite { " (lite renderer)" } else { "" }
        );
    }
    Ok((window, surface, context, gl, lite))
}

pub fn load_preset(renderer: &mut Renderer, cfg: &Config) -> Result<()> {
    let path = shaders::resolve(&cfg.preset)?;
    renderer.load_preset(&path, &cfg.params)
}

fn pick_monitor(event_loop: &ActiveEventLoop, index: usize) -> Option<MonitorHandle> {
    if index > 0 {
        if let Some(m) = event_loop.available_monitors().nth(index - 1) {
            return Some(m);
        }
        log::warn!("monitor {index} not found, using the primary one");
    }
    event_loop
        .primary_monitor()
        .or_else(|| event_loop.available_monitors().next())
}

fn monitor_label(m: &MonitorHandle) -> String {
    let s = m.size();
    format!(
        "{} ({}x{})",
        m.name().unwrap_or_else(|| "monitor".into()),
        s.width,
        s.height
    )
}

fn window_attributes(
    pos: winit::dpi::PhysicalPosition<i32>,
    size: winit::dpi::PhysicalSize<u32>,
) -> winit::window::WindowAttributes {
    let attrs = Window::default_attributes()
        .with_title("CRT Overlay")
        .with_decorations(false)
        .with_resizable(false)
        .with_window_level(WindowLevel::AlwaysOnTop)
        .with_position(pos)
        .with_inner_size(size)
        .with_active(false)
        .with_visible(false);
    #[cfg(target_os = "linux")]
    let attrs = {
        use winit::platform::x11::WindowAttributesExtX11;
        // Bypass the window manager: no frame, no taskbar entry, exact position.
        attrs.with_override_redirect(true)
    };
    #[cfg(windows)]
    let attrs = {
        use winit::platform::windows::WindowAttributesExtWindows;
        attrs.with_skip_taskbar(true)
    };
    attrs
}

#[cfg(target_os = "linux")]
fn create_capture(raw: RawWindowHandle, rect: Rect) -> Result<Box<dyn Capture>> {
    let xid = match raw {
        RawWindowHandle::Xlib(h) => h.window as u32,
        RawWindowHandle::Xcb(h) => h.window.get(),
        _ => bail!("not an X11 window"),
    };
    Ok(Box::new(capture::x11::X11Capture::new(xid, rect)?))
}

#[cfg(windows)]
fn create_capture(raw: RawWindowHandle, rect: Rect) -> Result<Box<dyn Capture>> {
    let RawWindowHandle::Win32(h) = raw else {
        bail!("not a Win32 window")
    };
    capture::windows::create(h.hwnd.get(), rect)
}

/// Tell the user about a fatal problem: there is no window to show it in.
fn show_error(text: &str) {
    #[cfg(windows)]
    {
        use windows::Win32::UI::WindowsAndMessaging::{
            MB_ICONERROR, MB_OK, MB_SETFOREGROUND, MessageBoxW,
        };
        use windows::core::HSTRING;
        unsafe {
            MessageBoxW(
                None,
                &HSTRING::from(text),
                &HSTRING::from("CRT Overlay"),
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND,
            );
        }
    }
    #[cfg(not(windows))]
    {
        // Best effort: a desktop notification, if notify-send is installed.
        let _ = std::process::Command::new("notify-send")
            .args(["-i", "dialog-error", "CRT Overlay", text])
            .spawn();
    }
}

fn open_in_editor(path: &std::path::Path) {
    #[cfg(windows)]
    let result = std::process::Command::new("notepad.exe").arg(path).spawn();
    #[cfg(not(windows))]
    let result = std::process::Command::new("xdg-open").arg(path).spawn();
    if let Err(e) = result {
        log::error!("cannot open {}: {e}", path.display());
    }
}
