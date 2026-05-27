use anyhow::Result;
use std::process::Command;

pub fn run() -> Result<()> {
    let checks: &[(&str, &[&str])] = &[
        ("arduino-cli", &["version"]),
        ("cmake",        &["--version"]),
        ("ninja",        &["--version"]),
        ("pkg-config",   &["--modversion", "sdl2"]),
    ];
    let mut all_ok = true;
    for (tool, args) in checks {
        let out = Command::new(tool).args(*args).output();
        match out {
            Ok(o) if o.status.success() => {
                let s = String::from_utf8_lossy(&o.stdout);
                let first = s.lines().next().unwrap_or("");
                println!("  ok    {tool}: {first}");
            }
            Ok(o) => {
                all_ok = false;
                println!("  fail  {tool}: exit {}", o.status);
            }
            Err(e) => {
                all_ok = false;
                println!("  fail  {tool}: {e}");
            }
        }
    }
    if !all_ok {
        anyhow::bail!("one or more host tools missing or broken");
    }
    Ok(())
}
