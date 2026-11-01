//! X11 capture through the Composite extension.
//!
//! Reading the root window would also return the overlay drawn on top of it,
//! so instead we redirect all top-level windows off-screen (automatic mode —
//! the X server or the running compositor still draws them as usual) and read
//! each window's own buffer, skipping the overlay. Windows are painted into the
//! desktop texture bottom to top; windows hidden behind an opaque window are
//! not read at all.

use super::{Capture, Rect, upload_bgra};
use crate::render::Target;
use anyhow::{Context, Result, anyhow, bail};
use glow::HasContext;
use std::time::{Duration, Instant};
use x11rb::connection::Connection;
use x11rb::protocol::composite::{self, ConnectionExt as _};
use x11rb::protocol::shm::{self, ConnectionExt as _};
use x11rb::protocol::xproto::{
    self, AtomEnum, ConfigureWindowAux, ConnectionExt as _, ImageFormat, ImageOrder, MapState,
    StackMode, WindowClass,
};
use x11rb::rust_connection::RustConnection;

struct ShmSegment {
    seg: shm::Seg,
    addr: *mut u8,
    size: usize,
}

struct Blender {
    program: glow::Program,
    quad: crate::glutil::Quad,
    tex: glow::Texture,
    tex_size: (u32, u32),
}

pub struct X11Capture {
    conn: RustConnection,
    root: xproto::Window,
    overlay: xproto::Window,
    monitor: Rect,
    shm: Option<ShmSegment>,
    root_pixmap_atom: xproto::Atom,
    blender: Option<Blender>,
    last_raise: Instant,
}

/// A top-level window that is (partly) on our monitor.
struct Visible {
    id: xproto::Window,
    /// Part of the window inside the monitor, in root coordinates.
    rect: Rect,
    /// Root-coordinate origin of the window's content.
    origin: (i32, i32),
    argb: bool,
}

impl X11Capture {
    pub fn new(overlay: u32, monitor: Rect) -> Result<Self> {
        let (conn, screen_num) = x11rb::connect(None).context("connect to X server")?;
        let screen = &conn.setup().roots[screen_num];
        let root = screen.root;

        if conn.setup().image_byte_order != ImageOrder::LSB_FIRST {
            bail!("big-endian X servers are not supported");
        }
        if !conn
            .setup()
            .pixmap_formats
            .iter()
            .any(|f| f.depth == 24 && f.bits_per_pixel == 32)
        {
            bail!("X server does not use 32 bits per pixel for depth 24");
        }

        let ver = conn
            .composite_query_version(0, 4)
            .context("Composite extension missing")?
            .reply()
            .context("Composite extension missing")?;
        log::info!("XComposite {}.{}", ver.major_version, ver.minor_version);
        // Automatic redirection: the screen still looks the same, but every
        // top-level window now has its own off-screen copy we can read even
        // where it is covered by the overlay. Undone by the server when we exit.
        conn.composite_redirect_subwindows(root, composite::Redirect::AUTOMATIC)?
            .check()
            .context("redirect windows")?;

        let root_pixmap_atom = conn.intern_atom(false, b"_XROOTPMAP_ID")?.reply()?.atom;

        let mut cap = X11Capture {
            conn,
            root,
            overlay,
            monitor,
            shm: None,
            root_pixmap_atom,
            blender: None,
            last_raise: Instant::now() - Duration::from_secs(1),
        };
        match cap.attach_shm() {
            Ok(()) => log::info!("using MIT-SHM for capture"),
            Err(e) => log::warn!("MIT-SHM unavailable ({e:#}), capture will be slower"),
        }
        Ok(cap)
    }

    fn attach_shm(&mut self) -> Result<()> {
        self.conn.shm_query_version()?.reply()?;
        let size = self.monitor.w as usize * self.monitor.h as usize * 4;
        unsafe {
            let id = libc::shmget(libc::IPC_PRIVATE, size, libc::IPC_CREAT | 0o600);
            if id < 0 {
                bail!("shmget failed");
            }
            let addr = libc::shmat(id, std::ptr::null(), 0);
            if addr as isize == -1 {
                libc::shmctl(id, libc::IPC_RMID, std::ptr::null_mut());
                bail!("shmat failed");
            }
            let seg = self.conn.generate_id()?;
            let attached = self
                .conn
                .shm_attach(seg, id as u32, false)
                .map(|c| c.check());
            // Mark for deletion now: it disappears once both sides detach.
            libc::shmctl(id, libc::IPC_RMID, std::ptr::null_mut());
            match attached {
                Ok(Ok(())) => {}
                _ => {
                    libc::shmdt(addr);
                    bail!("X server could not attach the segment (remote display?)");
                }
            }
            self.shm = Some(ShmSegment {
                seg,
                addr: addr as *mut u8,
                size,
            });
        }
        Ok(())
    }

    /// Read a rectangle of a drawable as BGRA rows and hand them to `f`.
    fn read<F: FnOnce(&[u8])>(
        &self,
        drawable: u32,
        x: i32,
        y: i32,
        w: u32,
        h: u32,
        f: F,
    ) -> Result<()> {
        if let Some(shm) = &self.shm {
            let len = w as usize * h as usize * 4;
            if len <= shm.size {
                let reply = self
                    .conn
                    .shm_get_image(
                        drawable,
                        x as i16,
                        y as i16,
                        w as u16,
                        h as u16,
                        !0,
                        ImageFormat::Z_PIXMAP.into(),
                        shm.seg,
                        0,
                    )?
                    .reply()?;
                if reply.depth < 24 {
                    bail!("unsupported depth {}", reply.depth);
                }
                let data = unsafe { std::slice::from_raw_parts(shm.addr, len) };
                f(data);
                return Ok(());
            }
        }
        let reply = self
            .conn
            .get_image(
                ImageFormat::Z_PIXMAP,
                drawable,
                x as i16,
                y as i16,
                w as u16,
                h as u16,
                !0,
            )?
            .reply()?;
        if reply.depth < 24 || reply.data.len() < w as usize * h as usize * 4 {
            bail!("unsupported image (depth {})", reply.depth);
        }
        f(&reply.data);
        Ok(())
    }

    /// Top-level windows on our monitor, bottom to top, minus the ones fully
    /// covered by an opaque window above them.
    fn visible_windows(&mut self) -> Result<Vec<Visible>> {
        let tree = self.conn.query_tree(self.root)?.reply()?;

        // Keep the overlay above everything (it is override-redirect, so the
        // window manager will not do it for us).
        if tree.children.last() != Some(&self.overlay)
            && self.last_raise.elapsed() > Duration::from_millis(100)
        {
            let aux = ConfigureWindowAux::new().stack_mode(StackMode::ABOVE);
            self.conn.configure_window(self.overlay, &aux)?;
            self.last_raise = Instant::now();
        }

        // Pipeline all attribute/geometry requests, then collect replies.
        let mut cookies = Vec::with_capacity(tree.children.len());
        for &w in tree.children.iter().filter(|&&w| w != self.overlay) {
            cookies.push((
                w,
                self.conn.get_window_attributes(w)?,
                self.conn.get_geometry(w)?,
            ));
        }
        let mut list = Vec::new();
        for (id, attrs, geom) in cookies {
            let (Ok(attrs), Ok(geom)) = (attrs.reply(), geom.reply()) else {
                continue;
            };
            if attrs.map_state != MapState::VIEWABLE || attrs.class != WindowClass::INPUT_OUTPUT {
                continue;
            }
            let bw = geom.border_width as i32;
            let origin = (geom.x as i32 + bw, geom.y as i32 + bw);
            let full = Rect {
                x: origin.0,
                y: origin.1,
                w: geom.width as u32,
                h: geom.height as u32,
            };
            if let Some(rect) = full.intersect(&self.monitor) {
                list.push(Visible {
                    id,
                    rect,
                    origin,
                    argb: geom.depth == 32,
                });
            }
        }

        // Occlusion: walk top to bottom, drop windows inside an opaque one above.
        let mut opaque: Vec<Rect> = Vec::new();
        let mut keep = vec![true; list.len()];
        for (i, v) in list.iter().enumerate().rev() {
            if opaque.iter().any(|o| o.contains(&v.rect)) {
                keep[i] = false;
            } else if !v.argb {
                opaque.push(v.rect);
            }
        }
        let mut i = 0;
        list.retain(|_| {
            i += 1;
            keep[i - 1]
        });
        Ok(list)
    }

    fn draw_background(&self, gl: &glow::Context, target: &Target) {
        unsafe {
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(target.fbo));
            gl.clear_color(0.0, 0.0, 0.0, 1.0);
            gl.clear(glow::COLOR_BUFFER_BIT);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
        }
        // Wallpaper set by most desktop environments / wallpaper setters.
        let Ok(Ok(prop)) = self
            .conn
            .get_property(
                false,
                self.root,
                self.root_pixmap_atom,
                AtomEnum::PIXMAP,
                0,
                1,
            )
            .map(|c| c.reply())
        else {
            return;
        };
        let Some(pixmap) = prop.value32().and_then(|mut v| v.next()) else {
            return;
        };
        let m = self.monitor;
        let _ = self.read(pixmap, m.x, m.y, m.w, m.h, |data| {
            upload_bgra(gl, target, (0, 0, m.w, m.h), m.w as usize * 4, data);
        });
    }

    fn blender(&mut self, gl: &glow::Context) -> Result<&mut Blender> {
        if self.blender.is_none() {
            self.blender = Some(Blender::new(gl)?);
        }
        Ok(self.blender.as_mut().unwrap())
    }
}

impl Capture for X11Capture {
    fn grab(&mut self, gl: &glow::Context, target: &Target) -> Result<bool> {
        let windows = self.visible_windows()?;
        let covered = windows.iter().any(|v| !v.argb && v.rect == self.monitor);
        if !covered {
            self.draw_background(gl, target);
        }
        for v in &windows {
            let (lx, ly) = (v.rect.x - v.origin.0, v.rect.y - v.origin.1);
            let (dx, dy) = (v.rect.x - self.monitor.x, v.rect.y - self.monitor.y);
            let (w, h) = (v.rect.w, v.rect.h);
            // A window can disappear between QueryTree and GetImage: skip it.
            let result = if v.argb {
                // Translucent window (shadows, rounded corners): blend it.
                let mut pixels = Vec::new();
                self.read(v.id, lx, ly, w, h, |d| {
                    pixels.extend_from_slice(&d[..w as usize * h as usize * 4])
                })
                .and_then(|_| {
                    let blender = self.blender(gl)?;
                    blender.draw(gl, target, (dx, dy, w, h), &pixels);
                    Ok(())
                })
            } else {
                self.read(v.id, lx, ly, w, h, |d| {
                    upload_bgra(gl, target, (dx, dy, w, h), w as usize * 4, d)
                })
            };
            if let Err(e) = result {
                log::trace!("skip window {:#x}: {e:#}", v.id);
            }
        }
        self.conn.flush()?;
        Ok(true)
    }
}

impl Drop for X11Capture {
    fn drop(&mut self) {
        if let Some(shm) = self.shm.take() {
            let _ = self.conn.shm_detach(shm.seg);
            let _ = self.conn.flush();
            unsafe { libc::shmdt(shm.addr as *const libc::c_void) };
        }
        let _ = self
            .conn
            .composite_unredirect_subwindows(self.root, composite::Redirect::AUTOMATIC);
        let _ = self.conn.flush();
    }
}

// GLSL 1.20 runs on OpenGL 2.1 and on 3.3 compatibility contexts;
// 3.30 core is used on core contexts.
const BLEND_VS_120: &str = "#version 120\nattribute vec2 pos;\nvarying vec2 uv;\nvoid main() { uv = pos; gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0); }";
const BLEND_FS_120: &str = "#version 120\nvarying vec2 uv;\nuniform sampler2D tex;\nvoid main() { gl_FragColor = texture2D(tex, uv); }";
const BLEND_VS_330: &str = "#version 330 core\nin vec2 pos;\nout vec2 uv;\nvoid main() { uv = pos; gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0); }";
const BLEND_FS_330: &str = "#version 330 core\nin vec2 uv;\nuniform sampler2D tex;\nout vec4 color;\nvoid main() { color = texture(tex, uv); }";

impl Blender {
    fn new(gl: &glow::Context) -> Result<Blender> {
        let (vs, fs) = if crate::glutil::modern(gl) {
            (BLEND_VS_330, BLEND_FS_330)
        } else {
            (BLEND_VS_120, BLEND_FS_120)
        };
        let program = crate::glutil::program(gl, vs, fs)?;
        let quad = crate::glutil::Quad::new(gl)?;
        unsafe {
            let tex = gl.create_texture().map_err(|e| anyhow!(e))?;
            gl.bind_texture(glow::TEXTURE_2D, Some(tex));
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MIN_FILTER,
                glow::NEAREST as i32,
            );
            gl.tex_parameter_i32(
                glow::TEXTURE_2D,
                glow::TEXTURE_MAG_FILTER,
                glow::NEAREST as i32,
            );
            gl.bind_texture(glow::TEXTURE_2D, None);
            Ok(Blender {
                program,
                quad,
                tex,
                tex_size: (0, 0),
            })
        }
    }

    /// Draw premultiplied BGRA pixels over `target` at the given rectangle.
    fn draw(
        &mut self,
        gl: &glow::Context,
        target: &Target,
        (x, y, w, h): (i32, i32, u32, u32),
        pixels: &[u8],
    ) {
        unsafe {
            gl.bind_texture(glow::TEXTURE_2D, Some(self.tex));
            gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 4);
            if self.tex_size != (w, h) {
                gl.tex_image_2d(
                    glow::TEXTURE_2D,
                    0,
                    glow::RGBA8 as i32,
                    w as i32,
                    h as i32,
                    0,
                    glow::BGRA,
                    glow::UNSIGNED_BYTE,
                    glow::PixelUnpackData::Slice(Some(pixels)),
                );
                self.tex_size = (w, h);
            } else {
                gl.tex_sub_image_2d(
                    glow::TEXTURE_2D,
                    0,
                    0,
                    0,
                    w as i32,
                    h as i32,
                    glow::BGRA,
                    glow::UNSIGNED_BYTE,
                    glow::PixelUnpackData::Slice(Some(pixels)),
                );
            }
            gl.bind_framebuffer(glow::FRAMEBUFFER, Some(target.fbo));
            gl.viewport(x, y, w as i32, h as i32);
            gl.enable(glow::BLEND);
            gl.blend_func(glow::ONE, glow::ONE_MINUS_SRC_ALPHA);
            gl.use_program(Some(self.program));
            gl.active_texture(glow::TEXTURE0);
            self.quad.draw(gl);
            gl.use_program(None);
            gl.disable(glow::BLEND);
            gl.bind_framebuffer(glow::FRAMEBUFFER, None);
            gl.bind_texture(glow::TEXTURE_2D, None);
        }
    }
}
