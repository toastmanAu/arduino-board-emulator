use anyhow::Result;
use std::path::Path;
use std::process::{Command, Stdio};

pub fn exec_sketch(binary: &Path) -> Result<i32> {
    let status = Command::new(binary)
        .stdin(Stdio::inherit())
        .stdout(Stdio::inherit())
        .stderr(Stdio::inherit())
        .status()?;
    Ok(status.code().unwrap_or(-1))
}
