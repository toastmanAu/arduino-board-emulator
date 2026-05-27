use crate::error::BoardGhostError;

pub const ALLOWLIST: &[&str] = &["LovyanGFX", "lvgl", "Adafruit_GFX"];

pub fn filter_allowed(libs: &[String]) -> Result<Vec<String>, BoardGhostError> {
    for lib in libs {
        if !is_allowed(lib) {
            return Err(BoardGhostError::UnsupportedLibrary { name: lib.clone() });
        }
    }
    Ok(libs.to_vec())
}

pub fn is_allowed(lib: &str) -> bool {
    ALLOWLIST.iter().any(|a| a.eq_ignore_ascii_case(lib))
}
