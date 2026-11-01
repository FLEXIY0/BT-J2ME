//! Load measurement shown in the tray: frame rate, GPU time per frame,
//! capture time per frame and the process' CPU usage.

use std::time::{Duration, Instant};

pub struct Stats {
    start: Instant,
    cpu_start: Duration,
    frames: u32,
    capture_ms: f64,
    gpu_ms: f64,
    gpu_samples: u32,
    last_log: Instant,
    cores: f64,
}

pub struct Report {
    pub fps: f64,
    pub gpu_ms: Option<f64>,
    pub capture_ms: f64,
    /// Share of the whole CPU (all cores) used by this process, in percent.
    pub cpu_percent: f64,
}

impl Report {
    pub fn short(&self) -> String {
        let gpu = match self.gpu_ms {
            Some(ms) => format!("GPU {ms:.1} мс/кадр"),
            None => "GPU —".into(),
        };
        format!(
            "{:.0} к/с · {gpu} · захват {:.1} мс · CPU {:.1}%",
            self.fps, self.capture_ms, self.cpu_percent
        )
    }
}

impl Stats {
    pub fn new() -> Stats {
        Stats {
            start: Instant::now(),
            cpu_start: process_cpu_time(),
            frames: 0,
            capture_ms: 0.0,
            gpu_ms: 0.0,
            gpu_samples: 0,
            last_log: Instant::now(),
            cores: std::thread::available_parallelism()
                .map(|n| n.get() as f64)
                .unwrap_or(1.0),
        }
    }

    pub fn frame(&mut self, capture: Duration, gpu_ms: Option<f64>) {
        self.frames += 1;
        self.capture_ms += capture.as_secs_f64() * 1000.0;
        if let Some(g) = gpu_ms {
            self.gpu_ms += g;
            self.gpu_samples += 1;
        }
    }

    /// Once a second: the averages since the previous report.
    pub fn report(&mut self) -> Option<Report> {
        let elapsed = self.start.elapsed();
        if elapsed < Duration::from_secs(1) || self.frames == 0 {
            return None;
        }
        let cpu_now = process_cpu_time();
        let cpu = cpu_now.saturating_sub(self.cpu_start).as_secs_f64();
        let r = Report {
            fps: self.frames as f64 / elapsed.as_secs_f64(),
            gpu_ms: (self.gpu_samples > 0).then(|| self.gpu_ms / self.gpu_samples as f64),
            capture_ms: self.capture_ms / self.frames as f64,
            cpu_percent: 100.0 * cpu / elapsed.as_secs_f64() / self.cores,
        };
        if self.last_log.elapsed() >= Duration::from_secs(10) {
            log::info!("load: {}", r.short());
            self.last_log = Instant::now();
        }
        *self = Stats {
            last_log: self.last_log,
            ..Stats::new()
        };
        self.cpu_start = cpu_now;
        Some(r)
    }
}

#[cfg(unix)]
fn process_cpu_time() -> Duration {
    let mut u: libc::rusage = unsafe { std::mem::zeroed() };
    if unsafe { libc::getrusage(libc::RUSAGE_SELF, &mut u) } != 0 {
        return Duration::ZERO;
    }
    let tv = |t: libc::timeval| Duration::new(t.tv_sec as u64, t.tv_usec as u32 * 1000);
    tv(u.ru_utime) + tv(u.ru_stime)
}

#[cfg(windows)]
fn process_cpu_time() -> Duration {
    use windows::Win32::Foundation::FILETIME;
    use windows::Win32::System::Threading::{GetCurrentProcess, GetProcessTimes};
    let (mut c, mut e, mut k, mut u) = (
        FILETIME::default(),
        FILETIME::default(),
        FILETIME::default(),
        FILETIME::default(),
    );
    if unsafe { GetProcessTimes(GetCurrentProcess(), &mut c, &mut e, &mut k, &mut u) }.is_err() {
        return Duration::ZERO;
    }
    let t = |f: FILETIME| ((f.dwHighDateTime as u64) << 32) | f.dwLowDateTime as u64;
    Duration::from_nanos((t(k) + t(u)) * 100)
}
