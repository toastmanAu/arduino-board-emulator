use anyhow::Result;
use std::path::Path;
use std::process::{Command, Stdio};

pub fn exec_sketch(binary: &Path, screenshot: Option<&Path>) -> Result<i32> {
    let mut cmd = Command::new(binary);
    cmd.stdin(Stdio::inherit())
       .stdout(Stdio::inherit())
       .stderr(Stdio::inherit());

    if let Some(path) = screenshot {
        cmd.env("BOARDGHOST_SCREENSHOT_PATH", path);
    }

    let mut child = cmd.spawn()?;

    // If a screenshot was requested, send SIGUSR1 after a short settle delay.
    if screenshot.is_some() {
        let pid = child.id();
        std::thread::spawn(move || {
            std::thread::sleep(std::time::Duration::from_millis(2000));
            #[cfg(unix)]
            unsafe { libc::kill(pid as i32, libc::SIGUSR1); }
        });
    }

    let status = child.wait()?;
    Ok(status.code().unwrap_or(-1))
}
