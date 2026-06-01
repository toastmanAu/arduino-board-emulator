use anyhow::Result;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

// Must match the destination used by `build::run` when mirroring the sketch
// `data/` dir — see spiffs_mirror::mirror_sketch_data. Kept in one place so the
// CLI's env var and the build stage can't drift apart.
pub(crate) fn assets_dir_for(project_dir: &Path) -> PathBuf {
    project_dir.join(".boardghost").join("sim-assets")
}

pub fn exec_sketch(binary: &Path, screenshot: Option<&Path>, project_dir: Option<&Path>) -> Result<i32> {
    let mut cmd = Command::new(binary);
    cmd.stdin(Stdio::inherit())
       .stdout(Stdio::inherit())
       .stderr(Stdio::inherit());

    if let Some(path) = screenshot {
        cmd.env("BOARDGHOST_SCREENSHOT_PATH", path);
    }

    // Set sim-assets and eeprom paths relative to the project directory.
    // This ensures SPIFFS/LittleFS/SD/EEPROM work regardless of where boardghost was invoked from.
    if let Some(proj) = project_dir {
        let proj_abs = proj.canonicalize().unwrap_or_else(|_| proj.to_path_buf());
        // Only set if not already in the environment (allow user override).
        if std::env::var("BOARDGHOST_ASSETS_DIR").is_err() {
            cmd.env("BOARDGHOST_ASSETS_DIR", assets_dir_for(&proj_abs));
        }
        if std::env::var("BOARDGHOST_EEPROM_PATH").is_err() {
            cmd.env("BOARDGHOST_EEPROM_PATH",
                    proj_abs.join(".boardghost").join("eeprom.bin"));
        }
        // UART backend writes FIFOs + capture files into <proj>/.boardghost/
        // — same anchor as eeprom + assets. Without this, the sketch's cwd
        // happens to be the CLI's invocation dir, which is rarely under
        // the project tree.
        if std::env::var("BOARDGHOST_UART_DIR").is_err() {
            cmd.env("BOARDGHOST_UART_DIR", proj_abs.join(".boardghost"));
        }
        // OTA firmware lands alongside; same fix.
        if std::env::var("BOARDGHOST_OTA_PATH").is_err() {
            cmd.env("BOARDGHOST_OTA_PATH",
                    proj_abs.join(".boardghost").join("ota-firmware.bin"));
        }
    }

    let mut child = cmd.spawn()?;

    // If a screenshot was requested, send SIGUSR1 after a short settle delay,
    // then wait for the screenshot file to appear and kill the sketch.
    // The sketch may never exit on its own (e.g. stuck in calibrateTouch), so
    // we kill it after the screenshot is captured.
    if let Some(screenshot_path) = screenshot {
        let pid = child.id();
        let screenshot_path = screenshot_path.to_path_buf();
        // Allow overriding the settle delay before screenshot via env var
        // (default 2000ms). Sketches with heavy setup() — JPG decode, touch
        // calibration, network connect — need more time to reach the visible
        // frame.
        let settle_ms: u64 = std::env::var("BOARDGHOST_SCREENSHOT_DELAY_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(2000);
        let screenshot_thread = std::thread::spawn(move || {
            std::thread::sleep(std::time::Duration::from_millis(settle_ms));
            // Trigger screenshot.
            #[cfg(unix)]
            unsafe { libc::kill(pid as i32, libc::SIGUSR1); }
            // Wait up to 5 seconds for the screenshot file to appear.
            let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
            while std::time::Instant::now() < deadline {
                std::thread::sleep(std::time::Duration::from_millis(200));
                if screenshot_path.exists() {
                    break;
                }
            }
            // Kill the sketch so boardghost exits cleanly.
            #[cfg(unix)]
            unsafe { libc::kill(pid as i32, libc::SIGTERM); }
        });
        let status = child.wait()?;
        let _ = screenshot_thread.join();
        return Ok(status.code().unwrap_or(-1));
    }

    let status = child.wait()?;
    Ok(status.code().unwrap_or(-1))
}

#[cfg(test)]
mod tests {
    use super::*;

    // Pins the BOARDGHOST_ASSETS_DIR computation against the build stage's
    // mirror destination so they can never silently drift again. If you change
    // either side, change both — and update this test.
    #[test]
    fn assets_dir_matches_build_mirror_root() {
        let project = Path::new("/proj");
        let out_root = project.join(".boardghost").join("ili9488_esp32s3_sim");
        let build_mirror_root = out_root.parent().unwrap().join("sim-assets");
        assert_eq!(assets_dir_for(project), build_mirror_root);
    }
}
