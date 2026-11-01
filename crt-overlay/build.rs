//! MinGW (windows-gnu) builds only: make the C++ runtime static.
//!
//! glslang-sys and spirv-cross-sys ask for `-lstdc++`, which rustc passes in
//! dynamic mode, so the exe ends up needing libstdc++-6.dll that normal
//! Windows machines do not have. Putting a copy of the static `libstdc++.a`
//! into a search directory that comes before the compiler's own makes the
//! linker pick the static archive instead.

use std::path::PathBuf;
use std::process::Command;

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    let env = std::env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    if os != "windows" || env != "gnu" {
        return;
    }
    let target = std::env::var("TARGET").unwrap();
    let cxx = std::env::var(format!("CXX_{}", target.replace('-', "_")))
        .or_else(|_| std::env::var("CXX"))
        .unwrap_or_else(|_| {
            if cfg!(windows) {
                "g++".into()
            } else {
                "x86_64-w64-mingw32-g++".into()
            }
        });
    let out = Command::new(&cxx)
        .arg("-print-file-name=libstdc++.a")
        .output();
    let lib = match out {
        Ok(o) if o.status.success() => PathBuf::from(String::from_utf8_lossy(&o.stdout).trim()),
        _ => {
            println!("cargo:warning=cannot run {cxx}; the exe may need libstdc++-6.dll");
            return;
        }
    };
    if !lib.is_absolute() || !lib.exists() {
        println!("cargo:warning=static libstdc++.a not found; the exe may need libstdc++-6.dll");
        return;
    }
    let dir = PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("static-libstdcxx");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::copy(&lib, dir.join("libstdc++.a")).unwrap();
    println!("cargo:rustc-link-search=native={}", dir.display());
}
