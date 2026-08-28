use crate::error::BoardGhostError;
use std::path::{Path, PathBuf};
use std::process::Command;

pub struct CompileOutput {
    pub binary: PathBuf,
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
    let jobs = build_jobs().to_string();
    let build = Command::new("cmake")
        .args(["--build", bin_dir.to_str().unwrap(), "-j", jobs.as_str()])
        .output()
        .map_err(|e| BoardGhostError::CmakeBuildFailed { exit: -1, stderr: e.to_string() })?;
    if !build.status.success() {
        return Err(BoardGhostError::CmakeBuildFailed {
            exit: build.status.code().unwrap_or(-1),
            stderr: format!("{}{}",
                String::from_utf8_lossy(&build.stdout),
                String::from_utf8_lossy(&build.stderr)),
        });
    }

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
