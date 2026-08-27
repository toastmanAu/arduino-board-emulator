use anyhow::Result;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

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
    // then wait for the screenshot file to appear.
    //
    // A sketch may never exit on its own (stuck in calibrateTouch, or an
    // ordinary forever-loop), so it has to be killed once the frame is
    // captured. But plenty of sketches DO end on their own — every example in
    // this repo finishes by calling exit(0) from loop() — and killing those the
    // instant the PNG lands truncates a run that was about to succeed. That is
    // a race between two lifecycle owners: whoever gets there first wins, and
    // which one that is depends on how long setup() happens to take.
    //
    // It bit tests/e2e/lgfx_codemod_smoke.sh, which asserts on the sketch's
    // final "done" line: the screenshot fired at the 2000ms settle and killed
    // the sketch before it reached that line, so the suite failed while the
    // captured screenshot was perfectly good.
    //
    // So: capture, then give the sketch a grace window to finish by itself, and
    // only SIGTERM if it is genuinely still running. `finished` is set the
    // moment child.wait() reaps, so we never signal a pid we have already
    // reaped.
    if let Some(screenshot_path) = screenshot {
        let pid = child.id();
        let screenshot_path = screenshot_path.to_path_buf();
        let finished = Arc::new(AtomicBool::new(false));
        let finished_t = Arc::clone(&finished);
        // Allow overriding the settle delay before screenshot via env var
        // (default 2000ms). Sketches with heavy setup() — JPG decode, touch
        // calibration, network connect — need more time to reach the visible
        // frame.
        let settle_ms: u64 = std::env::var("BOARDGHOST_SCREENSHOT_DELAY_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(2000);
        // How long to let a self-terminating sketch finish after the capture
        // before forcing it down. Overridable for sketches with slow teardown.
        let grace_ms: u64 = std::env::var("BOARDGHOST_SCREENSHOT_GRACE_MS")
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(5000);
        let screenshot_thread = std::thread::spawn(move || {
            std::thread::sleep(std::time::Duration::from_millis(settle_ms));
            // The sketch may already have run to completion during the settle;
            // signalling a reaped pid is at best useless and at worst hits an
            // unrelated process.
            if finished_t.load(Ordering::SeqCst) {
                return;
            }
            // Trigger screenshot.
            #[cfg(unix)]
            unsafe { libc::kill(pid as i32, libc::SIGUSR1); }
            // Wait up to 5 seconds for the screenshot file to appear.
            let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
            while std::time::Instant::now() < deadline {
                std::thread::sleep(std::time::Duration::from_millis(200));
                if screenshot_path.exists() || finished_t.load(Ordering::SeqCst) {
                    break;
                }
            }
            // Grace: a sketch that ends on its own gets to print its final
            // output and exit with its own status. Only force a sketch that is
            // still running when the window closes.
            let grace_deadline =
                std::time::Instant::now() + std::time::Duration::from_millis(grace_ms);
            while std::time::Instant::now() < grace_deadline {
                if finished_t.load(Ordering::SeqCst) {
                    return;
                }
                std::thread::sleep(std::time::Duration::from_millis(50));
            }
            #[cfg(unix)]
            unsafe { libc::kill(pid as i32, libc::SIGTERM); }
        });
        let status = child.wait()?;
        finished.store(true, Ordering::SeqCst);
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
