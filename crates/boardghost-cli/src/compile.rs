use crate::error::BoardGhostError;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

pub struct CompileOutput {
    pub binary: PathBuf,
}

/// Human-readable cause of a child process ending.
///
/// A process killed by a signal has NO exit code — `status.code()` is None, and
/// reporting that as "exit -1" is how a SIGTERM from an out-of-memory CI runner
/// came to look like an ordinary build failure.
fn describe_exit(code: Option<i32>, signal: Option<i32>) -> String {
    if let Some(sig) = signal {
        let name = match sig {
            2 => " (SIGINT)",
            9 => " (SIGKILL)",
            15 => " (SIGTERM)",
            _ => "",
        };
        return format!("killed by signal {sig}{name}");
    }
    match code {
        Some(c) => format!("exit {c}"),
        None => "terminated for an unknown reason".to_string(),
    }
}

/// Resolve the `-j` count from an optional override and the detected CPU count.
///
/// Never returns 0. `make -j0` is an error, and a MISSING count is worse: a
/// bare `-j` is what the Unix Makefiles generator reads as UNLIMITED — one
/// compiler process per ready target, with no cap at all.
fn jobs_from(raw: Option<&str>, detected: usize) -> usize {
    raw.and_then(|s| s.trim().parse::<usize>().ok())
        .filter(|n| *n > 0)
        .unwrap_or(detected)
        .max(1)
}

/// Parallelism for the sketch build.
///
/// `available_parallelism` honours cgroup CPU quota on Linux, so this is the
/// container/runner's real share rather than the host's core count.
/// `BOARDGHOST_BUILD_JOBS` overrides it.
fn build_jobs() -> usize {
    jobs_from(
        std::env::var("BOARDGHOST_BUILD_JOBS").ok().as_deref(),
        std::thread::available_parallelism().map(|n| n.get()).unwrap_or(1),
    )
}

pub fn cmake_configure_and_build(build_dir: &Path) -> Result<CompileOutput, BoardGhostError> {
    let bin_dir = build_dir.join("build");
    std::fs::create_dir_all(&bin_dir).map_err(|e| BoardGhostError::CmakeConfigureFailed {
        exit: -1, stderr: format!("mkdir {:?}: {e}", bin_dir),
    })?;

    let cfg = Command::new("cmake")
        .args(["-S", build_dir.to_str().unwrap(), "-B", bin_dir.to_str().unwrap()])
        .output()
        .map_err(|e| BoardGhostError::CmakeConfigureFailed { exit: -1, stderr: e.to_string() })?;
    if !cfg.status.success() {
        return Err(BoardGhostError::CmakeConfigureFailed {
            exit: cfg.status.code().unwrap_or(-1),
            stderr: format!("{}{}",
                String::from_utf8_lossy(&cfg.stdout),
                String::from_utf8_lossy(&cfg.stderr)),
        });
    }

    // The job count MUST be explicit. No generator is specified above, so CMake
    // picks its platform default — Unix Makefiles on Linux — and passes the
    // native-tool args straight through. `-j` alone therefore reaches make as
    // "unlimited", which spawns a compiler per ready target and exhausts memory
    // on any machine smaller than a workstation. It survived here for as long as
    // it did because it only ever ran on big local boxes; the first CI runner to
    // reach this line (4 vCPU / 16 GB) was killed with SIGTERM at 81 seconds,
    // silently, because a killed build never reaches the error path below that
    // would have printed its output.
    // The build's output goes to a FILE as it is produced, not into an in-memory
    // buffer collected at exit. `.output()` loses everything if the child — or
    // this process — is killed rather than exiting, which is exactly what
    // happens when a CI runner reclaims memory: the whole build was invisible,
    // and the only evidence left anywhere was "exit code 143". A file survives
    // the kill and can be read afterwards or uploaded as a CI artifact.
    let log_path = bin_dir.join("boardghost-build.log");
    let log = std::fs::File::create(&log_path).map_err(|e| BoardGhostError::CmakeBuildFailed {
        exit: -1, stderr: format!("create {:?}: {e}", log_path),
    })?;
    let log_err = log.try_clone().map_err(|e| BoardGhostError::CmakeBuildFailed {
        exit: -1, stderr: format!("dup {:?}: {e}", log_path),
    })?;

    let jobs = build_jobs().to_string();
    let status = Command::new("cmake")
        .args(["--build", bin_dir.to_str().unwrap(), "-j", jobs.as_str()])
        .stdout(Stdio::from(log))
        .stderr(Stdio::from(log_err))
        .status()
        .map_err(|e| BoardGhostError::CmakeBuildFailed { exit: -1, stderr: e.to_string() })?;
    if !status.success() {
        #[cfg(unix)]
        let signal = std::os::unix::process::ExitStatusExt::signal(&status);
        #[cfg(not(unix))]
        let signal: Option<i32> = None;
        let captured = std::fs::read_to_string(&log_path).unwrap_or_default();
        return Err(BoardGhostError::CmakeBuildFailed {
            exit: status.code().unwrap_or(-1),
            stderr: format!(
                "cmake --build -j {jobs}: {}\nfull log: {}\n{captured}",
                describe_exit(status.code(), signal),
                log_path.display(),
            ),
        });
    }
    eprintln!("\u{2192} Build log: {}", log_path.display());

    Ok(CompileOutput { binary: bin_dir.join("sketch") })
}

#[cfg(test)]
mod tests {
    use super::jobs_from;

    #[test]
    fn explicit_override_wins() {
        assert_eq!(jobs_from(Some("3"), 16), 3);
        assert_eq!(jobs_from(Some("  8 "), 16), 8);
    }

    #[test]
    fn unparseable_override_falls_back_to_detected() {
        assert_eq!(jobs_from(Some("all"), 6), 6);
        assert_eq!(jobs_from(Some(""), 6), 6);
        assert_eq!(jobs_from(None, 6), 6);
    }

    // A zero would become `make -j0`, which is an error; and the whole point of
    // this helper is that we never emit a bare `-j`, which means UNLIMITED.
    #[test]
    fn never_returns_zero() {
        assert_eq!(jobs_from(Some("0"), 6), 6);
        assert_eq!(jobs_from(None, 0), 1);
        assert_eq!(jobs_from(Some("0"), 0), 1);
    }
}

#[cfg(test)]
mod exit_tests {
    use super::describe_exit;

    #[test]
    fn plain_exit_code() {
        assert_eq!(describe_exit(Some(1), None), "exit 1");
        assert_eq!(describe_exit(Some(0), None), "exit 0");
    }

    // The case this exists for: a build killed by the CI runner reports no exit
    // code at all, and "exit -1" told us nothing about why.
    #[test]
    fn signal_is_named_and_wins_over_code() {
        assert_eq!(describe_exit(None, Some(15)), "killed by signal 15 (SIGTERM)");
        assert_eq!(describe_exit(None, Some(9)), "killed by signal 9 (SIGKILL)");
        assert_eq!(describe_exit(Some(-1), Some(15)), "killed by signal 15 (SIGTERM)");
    }

    #[test]
    fn unnamed_signal_still_reported() {
        assert_eq!(describe_exit(None, Some(31)), "killed by signal 31");
    }

    #[test]
    fn neither_is_not_a_panic() {
        assert_eq!(describe_exit(None, None), "terminated for an unknown reason");
    }
}
