use anyhow::Result;
use std::path::Path;
use std::process::{Command, Stdio};

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
            cmd.env("BOARDGHOST_ASSETS_DIR", proj_abs.join("sim-assets"));
        }
        if std::env::var("BOARDGHOST_EEPROM_PATH").is_err() {
            cmd.env("BOARDGHOST_EEPROM_PATH",
                    proj_abs.join(".boardghost").join("eeprom.bin"));
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
        let screenshot_thread = std::thread::spawn(move || {
            // Wait for sketch to initialise.
            std::thread::sleep(std::time::Duration::from_millis(2000));
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
