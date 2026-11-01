//! Windows capture.
//!
//! The overlay window is flagged `WDA_EXCLUDEFROMCAPTURE` (Windows 10 2004+),
//! so captured desktop images never contain the overlay itself.
//!
//! Two back-ends, tried in this order:
//! 1. Windows.Graphics.Capture (WGC) — works on hybrid-GPU laptops and with
//!    any adapter layout; this is what most screen recorders use today.
//! 2. DXGI Desktop Duplication — fallback. Fails with DXGI_ERROR_UNSUPPORTED
//!    on many hybrid (iGPU + dGPU) systems, which is why it is not first.
//! 3. GDI BitBlt — slowest, but available everywhere.

use super::{Capture, Rect, upload_bgra};
use crate::render::Target;
use anyhow::{Context, Result, anyhow, bail};
use std::time::{Duration, Instant};
use windows::Graphics::Capture::{
    Direct3D11CaptureFramePool, GraphicsCaptureAccess, GraphicsCaptureAccessKind,
    GraphicsCaptureItem, GraphicsCaptureSession,
};
use windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
use windows::Graphics::DirectX::DirectXPixelFormat;
use windows::Graphics::SizeInt32;
use windows::Win32::Foundation::{HMODULE, HWND, POINT};
use windows::Win32::Graphics::Direct3D::{
    D3D_DRIVER_TYPE, D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_UNKNOWN,
};
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::Dxgi::Common::*;
use windows::Win32::Graphics::Dxgi::*;
use windows::Win32::Graphics::Gdi::*;
use windows::Win32::System::WinRT::Direct3D11::{
    CreateDirect3D11DeviceFromDXGIDevice, IDirect3DDxgiInterfaceAccess,
};
use windows::Win32::System::WinRT::Graphics::Capture::IGraphicsCaptureItemInterop;
use windows::Win32::System::WinRT::{RO_INIT_MULTITHREADED, RoInitialize};
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::Interface;

/// Window tweaks for an input-transparent overlay that is not part of the
/// desktop capture. Called after creation and whenever winit may have
/// rewritten the window styles (e.g. after showing the window).
pub fn prepare_overlay_window(hwnd: isize) -> Result<()> {
    let hwnd = HWND(hwnd as *mut _);
    unsafe {
        SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE).context(
            "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed — Windows 10 2004 or newer is required",
        )?;
        let ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        let want = (WS_EX_LAYERED
            | WS_EX_TRANSPARENT
            | WS_EX_TOOLWINDOW
            | WS_EX_NOACTIVATE
            | WS_EX_TOPMOST)
            .0 as isize;
        let new = (ex | want) & !(WS_EX_APPWINDOW.0 as isize);
        if new != ex {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, new);
        }
        // A layered window without attributes is never shown.
        SetLayeredWindowAttributes(hwnd, Default::default(), 255, LWA_ALPHA)?;
        SetWindowPos(
            hwnd,
            Some(HWND_TOPMOST),
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED,
        )?;
    }
    Ok(())
}

/// Pick the best working capture back-end for the monitor.
pub fn create(hwnd: isize, monitor: Rect) -> Result<Box<dyn Capture>> {
    prepare_overlay_window(hwnd)?;
    let hwnd = HWND(hwnd as *mut _);
    let wgc_err = match WgcCapture::new(hwnd, monitor) {
        Ok(c) => {
            log::info!("capture: Windows.Graphics.Capture");
            return Ok(Box::new(c));
        }
        Err(e) => {
            log::warn!("Windows.Graphics.Capture unavailable: {e:#}");
            e
        }
    };
    let dxgi_err = match DxgiCapture::new(hwnd, monitor) {
        Ok(c) => {
            log::info!("capture: DXGI Desktop Duplication");
            return Ok(Box::new(c));
        }
        Err(e) => {
            log::warn!("Desktop Duplication unavailable: {e:#}");
            e
        }
    };
    match GdiCapture::new(hwnd, monitor) {
        Ok(c) => {
            log::info!("capture: GDI BitBlt (slow fallback)");
            Ok(Box::new(c))
        }
        Err(e) => Err(anyhow!(
            "no screen capture method works on this system.\n\
             Windows.Graphics.Capture: {wgc_err:#}\n\
             Desktop Duplication: {dxgi_err:#}\n\
             GDI: {e:#}"
        )),
    }
}

// ---------------------------------------------------------------- shared

fn create_device(adapter: Option<&IDXGIAdapter>) -> Result<(ID3D11Device, ID3D11DeviceContext)> {
    let mut device = None;
    let mut context = None;
    let driver: D3D_DRIVER_TYPE = if adapter.is_some() {
        D3D_DRIVER_TYPE_UNKNOWN
    } else {
        D3D_DRIVER_TYPE_HARDWARE
    };
    unsafe {
        D3D11CreateDevice(
            adapter,
            driver,
            HMODULE::default(),
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            None,
            D3D11_SDK_VERSION,
            Some(&mut device),
            None,
            Some(&mut context),
        )
        .context("D3D11CreateDevice")?;
    }
    Ok((
        device.ok_or_else(|| anyhow!("no D3D11 device"))?,
        context.ok_or_else(|| anyhow!("no D3D11 context"))?,
    ))
}

/// CPU-readable copy of a GPU texture, uploaded into the GL desktop texture.
struct Readback {
    device: ID3D11Device,
    context: ID3D11DeviceContext,
    staging: Option<(ID3D11Texture2D, u32, u32)>,
}

impl Readback {
    fn copy_to_staging(&mut self, src: &ID3D11Texture2D) -> Result<()> {
        let mut desc = D3D11_TEXTURE2D_DESC::default();
        unsafe { src.GetDesc(&mut desc) };
        if desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM {
            bail!("unsupported desktop format {:?}", desc.Format);
        }
        let reuse =
            matches!(&self.staging, Some((_, w, h)) if (*w, *h) == (desc.Width, desc.Height));
        if !reuse {
            let sdesc = D3D11_TEXTURE2D_DESC {
                Width: desc.Width,
                Height: desc.Height,
                MipLevels: 1,
                ArraySize: 1,
                Format: desc.Format,
                SampleDesc: DXGI_SAMPLE_DESC {
                    Count: 1,
                    Quality: 0,
                },
                Usage: D3D11_USAGE_STAGING,
                BindFlags: 0,
                CPUAccessFlags: D3D11_CPU_ACCESS_READ.0 as u32,
                MiscFlags: 0,
            };
            let mut tex = None;
            unsafe { self.device.CreateTexture2D(&sdesc, None, Some(&mut tex)) }
                .context("CreateTexture2D")?;
            self.staging = Some((
                tex.ok_or_else(|| anyhow!("no staging texture"))?,
                desc.Width,
                desc.Height,
            ));
        }
        let (staging, _, _) = self.staging.as_ref().unwrap();
        unsafe { self.context.CopyResource(staging, src) };
        Ok(())
    }

    /// Upload the top-left `w`x`h` of the staging copy.
    fn upload(&self, gl: &glow::Context, target: &Target, w: u32, h: u32) -> Result<()> {
        let (staging, sw, sh) = self
            .staging
            .as_ref()
            .ok_or_else(|| anyhow!("nothing captured"))?;
        let (w, h) = (w.min(*sw).min(target.width), h.min(*sh).min(target.height));
        if w == 0 || h == 0 {
            return Ok(());
        }
        let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
        unsafe {
            self.context
                .Map(staging, 0, D3D11_MAP_READ, 0, Some(&mut mapped))
        }
        .context("Map")?;
        let stride = mapped.RowPitch as usize;
        let data =
            unsafe { std::slice::from_raw_parts(mapped.pData as *const u8, stride * h as usize) };
        upload_bgra(gl, target, (0, 0, w, h), stride, data);
        unsafe { self.context.Unmap(staging, 0) };
        Ok(())
    }
}

/// The taskbar and other topmost windows can end up above the overlay.
struct TopmostKeeper {
    hwnd: HWND,
    last: Instant,
}

impl TopmostKeeper {
    fn tick(&mut self) {
        if self.last.elapsed() > Duration::from_millis(500) {
            self.last = Instant::now();
            unsafe {
                let _ = SetWindowPos(
                    self.hwnd,
                    Some(HWND_TOPMOST),
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
                );
            }
        }
    }
}

// ---------------------------------------------------------------- WGC

pub struct WgcCapture {
    readback: Readback,
    d3d: IDirect3DDevice,
    pool: Direct3D11CaptureFramePool,
    session: GraphicsCaptureSession,
    pool_size: SizeInt32,
    topmost: TopmostKeeper,
}

const FORMAT: DirectXPixelFormat = DirectXPixelFormat::B8G8R8A8UIntNormalized;

impl WgcCapture {
    fn new(hwnd: HWND, monitor: Rect) -> Result<Self> {
        if !GraphicsCaptureSession::IsSupported().unwrap_or(false) {
            bail!("not supported on this Windows version");
        }
        request_borderless_capture();

        let (device, context) = create_device(None)?;
        let dxgi: IDXGIDevice = device.cast()?;
        let d3d: IDirect3DDevice =
            unsafe { CreateDirect3D11DeviceFromDXGIDevice(&dxgi) }?.cast()?;

        let center = POINT {
            x: monitor.x + monitor.w as i32 / 2,
            y: monitor.y + monitor.h as i32 / 2,
        };
        let hmon = unsafe { MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST) };
        let interop = windows::core::factory::<GraphicsCaptureItem, IGraphicsCaptureItemInterop>()?;
        let item: GraphicsCaptureItem =
            unsafe { interop.CreateForMonitor(hmon) }.context("CreateForMonitor")?;
        let size = item.Size()?;
        log::info!("WGC item {}x{}", size.Width, size.Height);

        let pool = Direct3D11CaptureFramePool::CreateFreeThreaded(&d3d, FORMAT, 2, size)?;
        let session = pool.CreateCaptureSession(&item)?;
        // The real cursor is drawn by Windows on top of the overlay anyway.
        let _ = session.SetIsCursorCaptureEnabled(false);
        // Windows 11: no yellow "being captured" border. Older builds ignore it.
        if let Err(e) = session.SetIsBorderRequired(false) {
            log::debug!("cannot disable capture border: {e}");
        }
        session.StartCapture()?;

        Ok(WgcCapture {
            readback: Readback {
                device,
                context,
                staging: None,
            },
            d3d,
            pool,
            session,
            pool_size: size,
            topmost: TopmostKeeper {
                hwnd,
                last: Instant::now(),
            },
        })
    }
}

/// Windows 11 asks apps to request "borderless" capture before turning the
/// yellow border off. Unpackaged apps are allowed; the call may block, so it
/// runs on a helper thread with a short timeout.
fn request_borderless_capture() {
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || {
        let r = (|| -> windows::core::Result<()> {
            unsafe { RoInitialize(RO_INIT_MULTITHREADED) }?;
            GraphicsCaptureAccess::RequestAccessAsync(GraphicsCaptureAccessKind::Borderless)?
                .join()?;
            Ok(())
        })();
        let _ = tx.send(r);
    });
    match rx.recv_timeout(Duration::from_secs(2)) {
        Ok(Ok(())) => {}
        Ok(Err(e)) => log::debug!("borderless capture request: {e}"),
        Err(_) => log::debug!("borderless capture request timed out"),
    }
}

impl Capture for WgcCapture {
    fn grab(&mut self, gl: &glow::Context, target: &Target) -> Result<bool> {
        self.topmost.tick();
        // Take the newest frame; WGC only delivers frames when the screen changes.
        let mut latest = None;
        while let Ok(frame) = self.pool.TryGetNextFrame() {
            if let Some(old) = latest.replace(frame) {
                let _ = old.Close();
            }
        }
        let Some(frame) = latest else {
            return Ok(false);
        };

        let result = (|| -> Result<SizeInt32> {
            let size = frame.ContentSize()?;
            let access: IDirect3DDxgiInterfaceAccess = frame.Surface()?.cast()?;
            let tex: ID3D11Texture2D = unsafe { access.GetInterface() }?;
            self.readback.copy_to_staging(&tex)?;
            Ok(size)
        })();
        let _ = frame.Close();
        let size = result?;

        if size.Width != self.pool_size.Width || size.Height != self.pool_size.Height {
            // Resolution / orientation changed.
            log::info!("capture size changed to {}x{}", size.Width, size.Height);
            self.pool.Recreate(&self.d3d, FORMAT, 2, size)?;
            self.pool_size = size;
        }
        self.readback.upload(
            gl,
            target,
            size.Width.max(0) as u32,
            size.Height.max(0) as u32,
        )?;
        Ok(true)
    }
}

impl Drop for WgcCapture {
    fn drop(&mut self) {
        let _ = self.session.Close();
        let _ = self.pool.Close();
    }
}

// ---------------------------------------------------------------- DXGI

pub struct DxgiCapture {
    readback: Readback,
    output: IDXGIOutput1,
    dup: Option<IDXGIOutputDuplication>,
    last_retry: Instant,
    topmost: TopmostKeeper,
}

impl DxgiCapture {
    fn new(hwnd: HWND, monitor: Rect) -> Result<Self> {
        let (adapter, output) = find_output(monitor)?;
        let (device, context) = create_device(Some(&adapter.cast()?))?;
        let mut cap = DxgiCapture {
            readback: Readback {
                device,
                context,
                staging: None,
            },
            output,
            dup: None,
            last_retry: Instant::now(),
            topmost: TopmostKeeper {
                hwnd,
                last: Instant::now(),
            },
        };
        cap.duplicate()?;
        Ok(cap)
    }

    fn duplicate(&mut self) -> Result<()> {
        self.dup = None;
        self.last_retry = Instant::now();
        let dup = unsafe { self.output.DuplicateOutput(&self.readback.device) }
            .context("DuplicateOutput")?;
        let desc = unsafe { dup.GetDesc() };
        log::info!(
            "desktop duplication {}x{} format {:?} rotation {:?}",
            desc.ModeDesc.Width,
            desc.ModeDesc.Height,
            desc.ModeDesc.Format,
            desc.Rotation
        );
        self.dup = Some(dup);
        Ok(())
    }
}

impl Capture for DxgiCapture {
    fn grab(&mut self, gl: &glow::Context, target: &Target) -> Result<bool> {
        self.topmost.tick();
        if self.dup.is_none() {
            // Lost after a mode change, UAC prompt, lock screen...: retry now and then.
            if self.last_retry.elapsed() < Duration::from_millis(500) {
                return Ok(false);
            }
            if let Err(e) = self.duplicate() {
                log::debug!("duplication not available yet: {e:#}");
                return Ok(false);
            }
        }
        let dup = self.dup.clone().unwrap();

        let mut info = DXGI_OUTDUPL_FRAME_INFO::default();
        let mut resource: Option<IDXGIResource> = None;
        match unsafe { dup.AcquireNextFrame(0, &mut info, &mut resource) } {
            Ok(()) => {}
            Err(e) if e.code() == DXGI_ERROR_WAIT_TIMEOUT => return Ok(false),
            Err(e) if e.code() == DXGI_ERROR_ACCESS_LOST || e.code() == DXGI_ERROR_INVALID_CALL => {
                log::info!("desktop duplication lost ({e}), recreating");
                self.dup = None;
                return Ok(false);
            }
            Err(e) => bail!("AcquireNextFrame: {e}"),
        }

        let result = (|| -> Result<bool> {
            // Only the mouse pointer moved: the image is unchanged.
            if info.LastPresentTime == 0 || info.AccumulatedFrames == 0 {
                return Ok(false);
            }
            let frame: ID3D11Texture2D = resource
                .as_ref()
                .ok_or_else(|| anyhow!("no frame"))?
                .cast()?;
            self.readback.copy_to_staging(&frame)?;
            Ok(true)
        })();
        unsafe {
            let _ = dup.ReleaseFrame();
        }
        if !result? {
            return Ok(false);
        }
        self.readback.upload(gl, target, u32::MAX, u32::MAX)?;
        Ok(true)
    }
}

// ---------------------------------------------------------------- GDI

pub struct GdiCapture {
    monitor: Rect,
    screen_dc: HDC,
    mem_dc: HDC,
    bitmap: HBITMAP,
    old: HGDIOBJ,
    bits: *const u8,
    topmost: TopmostKeeper,
}

impl GdiCapture {
    fn new(hwnd: HWND, monitor: Rect) -> Result<Self> {
        unsafe {
            let screen_dc = GetDC(None);
            if screen_dc.is_invalid() {
                bail!("GetDC failed");
            }
            let mem_dc = CreateCompatibleDC(Some(screen_dc));
            let info = BITMAPINFO {
                bmiHeader: BITMAPINFOHEADER {
                    biSize: std::mem::size_of::<BITMAPINFOHEADER>() as u32,
                    biWidth: monitor.w as i32,
                    biHeight: -(monitor.h as i32), // top-down rows
                    biPlanes: 1,
                    biBitCount: 32,
                    biCompression: BI_RGB.0,
                    ..Default::default()
                },
                ..Default::default()
            };
            let mut bits = std::ptr::null_mut();
            let bitmap = CreateDIBSection(Some(mem_dc), &info, DIB_RGB_COLORS, &mut bits, None, 0)
                .context("CreateDIBSection")?;
            let old = SelectObject(mem_dc, bitmap.into());
            Ok(GdiCapture {
                monitor,
                screen_dc,
                mem_dc,
                bitmap,
                old,
                bits: bits as *const u8,
                topmost: TopmostKeeper {
                    hwnd,
                    last: Instant::now(),
                },
            })
        }
    }
}

impl Capture for GdiCapture {
    fn grab(&mut self, gl: &glow::Context, target: &Target) -> Result<bool> {
        self.topmost.tick();
        let m = self.monitor;
        unsafe {
            BitBlt(
                self.mem_dc,
                0,
                0,
                m.w as i32,
                m.h as i32,
                Some(self.screen_dc),
                m.x,
                m.y,
                SRCCOPY | CAPTUREBLT,
            )
            .context("BitBlt")?;
            let _ = GdiFlush();
            let data = std::slice::from_raw_parts(self.bits, m.w as usize * m.h as usize * 4);
            let (w, h) = (m.w.min(target.width), m.h.min(target.height));
            upload_bgra(gl, target, (0, 0, w, h), m.w as usize * 4, data);
        }
        Ok(true)
    }
}

impl Drop for GdiCapture {
    fn drop(&mut self) {
        unsafe {
            SelectObject(self.mem_dc, self.old);
            let _ = DeleteObject(self.bitmap.into());
            let _ = DeleteDC(self.mem_dc);
            ReleaseDC(None, self.screen_dc);
        }
    }
}

/// Find the DXGI adapter/output whose desktop rectangle matches the monitor.
fn find_output(monitor: Rect) -> Result<(IDXGIAdapter1, IDXGIOutput1)> {
    let factory: IDXGIFactory1 = unsafe { CreateDXGIFactory1() }.context("CreateDXGIFactory1")?;
    let mut fallback = None;
    for a in 0.. {
        let Ok(adapter) = (unsafe { factory.EnumAdapters1(a) }) else {
            break;
        };
        for o in 0.. {
            let Ok(output) = (unsafe { adapter.EnumOutputs(o) }) else {
                break;
            };
            let desc = unsafe { output.GetDesc() }?;
            let r = desc.DesktopCoordinates;
            let output1: IDXGIOutput1 = output.cast()?;
            if r.left == monitor.x && r.top == monitor.y {
                return Ok((adapter, output1));
            }
            if fallback.is_none() && desc.AttachedToDesktop.as_bool() {
                fallback = Some((adapter.clone(), output1));
            }
        }
    }
    log::warn!(
        "no DXGI output at {},{}; using the first one",
        monitor.x,
        monitor.y
    );
    fallback.ok_or_else(|| anyhow!("no display outputs found"))
}
