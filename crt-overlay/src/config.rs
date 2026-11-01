//! User settings stored in `config.toml`.
//!
//! The file is created with comments on first start. Changes made from the tray
//! menu are written back with `toml_edit`, so user comments survive.

use anyhow::{Context, Result};
use serde::Deserialize;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

pub const DEFAULT_CONFIG: &str = r#"# CRT Overlay settings.
# Most of this can be changed from the tray menu; the file is re-read on
# "Reload shader and settings".

# Bundled preset:
#   crt-tube             - CRT model fitted to a photo of a real CRT monitor
#                          (beam, bright-dependent scanlines, aperture grille,
#                          convergence, glow). Default.
#   crt-guest-advanced, crt-guest-advanced-fast, crt-guest-advanced-fastest,
#   crt-guest-advanced-hd, crt-guest-advanced-ntsc, crt-guest-advanced-pal
#                        - CRT-Guest-Advanced by guest(r).
# ...or a full path to any RetroArch .slangp preset.
# The desktop is shrunk to "lines" rows; each row is drawn as one scanline.
preset = "crt-tube"

# 0 = primary monitor, 1..N = monitor number as listed in the tray menu.
monitor = 0

# Number of scanlines from top to bottom of the screen ("CRT resolution").
# 0 = auto: one line per 4.5 screen pixels (240 on 1080p, 320 on 1440p,
# 480 on 4K), like a 240p console on a TV. Fewer lines = bigger "pixels".
lines = 0

# Renderer: "auto" = full (OpenGL 3.3) if available, otherwise lite
# (OpenGL 2.1, for old GPUs such as Intel GMA 4500; draws CRT Tube only).
# "full" or "lite" forces one.
renderer = "auto"

# Frame-rate cap. 0 = follow the monitor refresh rate (vsync); the lite
# renderer uses 30 by default.
max_fps = 0

# Start with the shader turned on.
enabled = true

# Shader parameter overrides (#pragma parameter names from the .slang files).
# crt-tube (see shaders/shaders/tube/crt-tube.slang for all of them):
#   TUBE_HBEAM        # horizontal beam width, source px
#   TUBE_WMIN / TUBE_WMAX   # scanline thickness on dark / bright colours
#   TUBE_MASK_STR     # aperture grille strength
#   TUBE_MASK_PITCH   # grille triad pitch, in scanlines
#   TUBE_CONV_R / TUBE_CONV_B   # convergence error of red / blue, source px
#   TUBE_GLOW, TUBE_GLOW_SIZE   # halation
#   TUBE_BRIGHT, TUBE_SAT, TUBE_GAMMA_IN, TUBE_GAMMA_OUT
# crt-guest-advanced: warpX / warpY (curvature), shadowMask, glow, bloom...
[params]
# guest-advanced: do not switch to interlaced mode on high line counts.
inter = 800.0
"#;

#[derive(Debug, Clone, Deserialize)]
#[serde(default)]
pub struct Config {
    pub preset: String,
    pub monitor: usize,
    pub lines: u32,
    pub max_fps: u32,
    pub renderer: String,
    pub enabled: bool,
    pub params: BTreeMap<String, f32>,
}

impl Default for Config {
    // Must match DEFAULT_CONFIG (checked by a test). Not parsed from it:
    // `#[serde(default)]` calls this function, so that would recurse.
    fn default() -> Self {
        Config {
            preset: "crt-tube".into(),
            monitor: 0,
            lines: 0,
            max_fps: 0,
            renderer: "auto".into(),
            enabled: true,
            params: BTreeMap::from([("inter".to_string(), 800.0)]),
        }
    }
}

impl Config {
    pub fn default_path() -> PathBuf {
        dirs::config_dir()
            .unwrap_or_else(|| PathBuf::from("."))
            .join("crt-overlay")
            .join("config.toml")
    }

    /// Load the config, creating the file with defaults if it does not exist.
    /// A broken file is reported and defaults are used, so the overlay still starts.
    pub fn load_or_create(path: &Path) -> Config {
        if !path.exists() {
            if let Some(dir) = path.parent() {
                let _ = std::fs::create_dir_all(dir);
            }
            if let Err(e) = std::fs::write(path, DEFAULT_CONFIG) {
                log::warn!("cannot write {}: {e}", path.display());
            }
            return Config::default();
        }
        let text = std::fs::read_to_string(path).unwrap_or_default();
        if is_old_format(&text) {
            // v0.1 files (with "scale", default preset guest-advanced):
            // keep a copy and start from the new defaults.
            let backup = path.with_extension("toml.old");
            let _ = std::fs::write(&backup, &text);
            let _ = std::fs::write(path, DEFAULT_CONFIG);
            log::info!(
                "old config moved to {}; new defaults written",
                backup.display()
            );
            return Config::default();
        }
        match toml::from_str::<Config>(&text).context("parse") {
            Ok(c) => c,
            Err(e) => {
                log::error!("bad config {}: {e:#}; using defaults", path.display());
                Config::default()
            }
        }
    }

    /// Scanline count for a screen `height` pixels tall.
    pub fn effective_lines(&self, height: u32) -> u32 {
        if self.lines > 0 {
            self.lines.min(height)
        } else {
            auto_lines(height)
        }
    }
}

/// One scanline per 4.5 screen pixels — 240 lines on 1080p, like a 240p
/// console on a TV — kept in a CRT-like range.
pub fn auto_lines(height: u32) -> u32 {
    ((height as f32 / 4.5).round() as u32)
        .clamp(200, 540)
        .min(height.max(1))
}

fn is_old_format(text: &str) -> bool {
    text.parse::<toml_edit::DocumentMut>()
        .map(|d| d.get("scale").is_some() && d.get("lines").is_none())
        .unwrap_or(false)
}

/// Update a single top-level key in the config file, keeping comments intact.
pub fn save_value(path: &Path, key: &str, value: toml_edit::Value) -> Result<()> {
    let text = std::fs::read_to_string(path).unwrap_or_else(|_| DEFAULT_CONFIG.to_string());
    let mut doc: toml_edit::DocumentMut = text.parse().context("parse config")?;
    // toml_edit keeps new top-level keys above the [params] table.
    doc[key] = toml_edit::Item::Value(value);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir)?;
    }
    std::fs::write(path, doc.to_string())?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_config_parses() {
        let c: Config = toml::from_str(DEFAULT_CONFIG).unwrap();
        let d = Config::default();
        assert_eq!(
            (
                &c.preset, c.monitor, c.lines, c.max_fps, c.enabled, &c.params
            ),
            (
                &d.preset, d.monitor, d.lines, d.max_fps, d.enabled, &d.params
            )
        );
        assert_eq!(c.preset, "crt-tube");
    }

    #[test]
    fn line_count() {
        let mut c = Config::default();
        assert_eq!(c.effective_lines(1080), 240);
        assert_eq!(c.effective_lines(1440), 320);
        assert_eq!(c.effective_lines(2160), 480);
        assert_eq!(c.effective_lines(720), 200);
        c.lines = 288;
        assert_eq!(c.effective_lines(1080), 288);
    }

    #[test]
    fn save_keeps_comments_and_migrates() {
        let dir = std::env::temp_dir().join(format!("crt-overlay-test-{}", std::process::id()));
        let path = dir.join("config.toml");
        std::fs::create_dir_all(&dir).unwrap();
        std::fs::write(&path, DEFAULT_CONFIG).unwrap();
        save_value(&path, "lines", 288i64.into()).unwrap();
        save_value(&path, "preset", "crt-guest-advanced-hd".into()).unwrap();
        let text = std::fs::read_to_string(&path).unwrap();
        assert!(text.contains("# Number of scanlines"));
        let c = Config::load_or_create(&path);
        assert_eq!(c.lines, 288);
        assert_eq!(c.preset, "crt-guest-advanced-hd");
        assert_eq!(c.params.get("inter"), Some(&800.0));

        std::fs::write(&path, "preset = \"crt-guest-advanced\"\nscale = 2.0\n").unwrap();
        let c = Config::load_or_create(&path);
        assert_eq!(c.preset, "crt-tube");
        assert!(dir.join("config.toml.old").exists());
        std::fs::remove_dir_all(&dir).unwrap();
    }
}
