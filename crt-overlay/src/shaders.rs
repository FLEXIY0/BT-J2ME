//! Bundled CRT-Guest-Advanced presets (from libretro/slang-shaders).
//!
//! The shader files are embedded into the binary and unpacked into the cache
//! directory on first use, because librashader loads presets from disk.

use anyhow::{Context, Result, bail};
use include_dir::{Dir, include_dir};
use std::path::{Path, PathBuf};

static SHADERS: Dir<'static> = include_dir!("$CARGO_MANIFEST_DIR/shaders");

/// (preset file stem, menu label)
pub const BUNDLED: &[(&str, &str)] = &[
    ("crt-tube", "CRT Tube — как настоящий ЭЛТ"),
    ("crt-guest-advanced", "CRT-Guest-Advanced"),
    ("crt-guest-advanced-fast", "CRT-Guest-Advanced Fast"),
    ("crt-guest-advanced-fastest", "CRT-Guest-Advanced Fastest"),
    ("crt-guest-advanced-hd", "CRT-Guest-Advanced HD"),
    ("crt-guest-advanced-ntsc", "CRT-Guest-Advanced NTSC"),
    ("crt-guest-advanced-pal", "CRT-Guest-Advanced PAL"),
];

/// Cache directory named after a hash of the embedded files, so a build
/// with changed shaders never reuses stale files from an older build.
fn unpack_dir() -> PathBuf {
    use std::hash::{Hash, Hasher};
    fn walk(dir: &Dir, h: &mut impl Hasher) {
        for f in dir.files() {
            f.path().hash(h);
            f.contents().hash(h);
        }
        for d in dir.dirs() {
            walk(d, h);
        }
    }
    let mut h = std::collections::hash_map::DefaultHasher::new();
    walk(&SHADERS, &mut h);
    dirs::cache_dir()
        .unwrap_or_else(std::env::temp_dir)
        .join("crt-overlay")
        .join(format!("shaders-{:016x}", h.finish()))
}

fn ensure_unpacked() -> Result<PathBuf> {
    let dir = unpack_dir();
    let marker = dir.join(".complete");
    if !marker.exists() {
        std::fs::create_dir_all(&dir).with_context(|| format!("create {}", dir.display()))?;
        SHADERS
            .extract(&dir)
            .with_context(|| format!("unpack shaders into {}", dir.display()))?;
        std::fs::write(&marker, b"")?;
        log::info!("shaders unpacked into {}", dir.display());
    }
    Ok(dir)
}

/// Resolve the `preset` config value to a .slangp file on disk.
pub fn resolve(preset: &str) -> Result<PathBuf> {
    if BUNDLED.iter().any(|(name, _)| *name == preset) {
        let path = ensure_unpacked()?.join(format!("{preset}.slangp"));
        return Ok(path);
    }
    let path = Path::new(preset);
    if path.is_file() {
        return Ok(path.to_path_buf());
    }
    bail!("preset '{preset}' is neither a bundled preset nor an existing .slangp file")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_bundled_presets_are_embedded() {
        for (name, _) in BUNDLED {
            assert!(
                SHADERS.get_file(format!("{name}.slangp")).is_some(),
                "{name}"
            );
        }
    }
}
