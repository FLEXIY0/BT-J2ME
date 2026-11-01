//! Screen capture back-ends. Each one fills `Renderer::desktop` with the
//! contents of one monitor *without* the overlay window itself, otherwise the
//! shader would be fed its own output.
//!
//! * Windows: DXGI Desktop Duplication, with the overlay window marked
//!   `WDA_EXCLUDEFROMCAPTURE` so DWM leaves it out of the duplicated image.
//! * Linux/X11: XComposite. Every top-level window except the overlay is read
//!   from its off-screen buffer and stacked in a texture, bottom to top.

use crate::render::Target;
use anyhow::Result;
use glow::HasContext;

#[cfg(windows)]
pub mod windows;
#[cfg(target_os = "linux")]
pub mod x11;

/// Monitor rectangle in desktop (virtual screen) pixels.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: u32,
    pub h: u32,
}

// Only the X11 back-end composes windows itself.
#[cfg_attr(not(target_os = "linux"), allow(dead_code))]
impl Rect {
    pub fn intersect(&self, o: &Rect) -> Option<Rect> {
        let x0 = self.x.max(o.x);
        let y0 = self.y.max(o.y);
        let x1 = (self.x + self.w as i32).min(o.x + o.w as i32);
        let y1 = (self.y + self.h as i32).min(o.y + o.h as i32);
        (x1 > x0 && y1 > y0).then(|| Rect {
            x: x0,
            y: y0,
            w: (x1 - x0) as u32,
            h: (y1 - y0) as u32,
        })
    }

    pub fn contains(&self, o: &Rect) -> bool {
        o.x >= self.x
            && o.y >= self.y
            && o.x + o.w as i32 <= self.x + self.w as i32
            && o.y + o.h as i32 <= self.y + self.h as i32
    }
}

pub trait Capture {
    /// Update `target` with the current screen contents. Returns `false` when
    /// nothing changed since the previous call (the texture is left as is).
    fn grab(&mut self, gl: &glow::Context, target: &Target) -> Result<bool>;
}

/// Upload a BGRA pixel block (rows top-first) into `target` at (x, y).
/// `stride` is the source row length in bytes.
pub fn upload_bgra(
    gl: &glow::Context,
    target: &Target,
    (x, y, w, h): (i32, i32, u32, u32),
    stride: usize,
    data: &[u8],
) {
    debug_assert!(
        stride.is_multiple_of(4) && data.len() >= stride * (h as usize - 1) + w as usize * 4
    );
    unsafe {
        gl.bind_texture(glow::TEXTURE_2D, Some(target.tex));
        gl.pixel_store_i32(glow::UNPACK_ALIGNMENT, 4);
        gl.pixel_store_i32(glow::UNPACK_ROW_LENGTH, (stride / 4) as i32);
        gl.tex_sub_image_2d(
            glow::TEXTURE_2D,
            0,
            x,
            y,
            w as i32,
            h as i32,
            glow::BGRA,
            glow::UNSIGNED_BYTE,
            glow::PixelUnpackData::Slice(Some(data)),
        );
        gl.pixel_store_i32(glow::UNPACK_ROW_LENGTH, 0);
        gl.bind_texture(glow::TEXTURE_2D, None);
    }
}

#[cfg(test)]
mod tests {
    use super::Rect;

    #[test]
    fn rect_ops() {
        let mon = Rect {
            x: 0,
            y: 0,
            w: 1920,
            h: 1080,
        };
        let win = Rect {
            x: -10,
            y: 1000,
            w: 100,
            h: 200,
        };
        assert_eq!(
            mon.intersect(&win),
            Some(Rect {
                x: 0,
                y: 1000,
                w: 90,
                h: 80
            })
        );
        assert_eq!(
            mon.intersect(&Rect {
                x: 1920,
                y: 0,
                w: 5,
                h: 5
            }),
            None
        );
        assert!(mon.contains(&Rect {
            x: 10,
            y: 10,
            w: 10,
            h: 10
        }));
        assert!(
            !Rect {
                x: 10,
                y: 10,
                w: 10,
                h: 10
            }
            .contains(&mon)
        );
    }
}
