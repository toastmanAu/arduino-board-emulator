use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

// The "live" EEPROM the sketch reads/writes at runtime — created and updated
// by sim_eeprom.cpp. Build-cache location, not committable.
pub fn live_path(project_dir: &Path) -> PathBuf {
    project_dir.join(".boardghost").join("eeprom.bin")
}

// The convention for a committable seed shipped with a sketch. Project-rooted
// so it survives `.boardghost/` being wiped.
pub fn default_seed_path(project_dir: &Path) -> PathBuf {
    project_dir.join("eeprom.seed.bin")
}

/// Copies the current live EEPROM to `dest` (creating parent dirs). Errors
/// if no live EEPROM exists yet — the sketch must have been run and reached
/// `EEPROM.commit()` at least once.
pub fn save(project_dir: &Path, dest: &Path) -> Result<()> {
    let live = live_path(project_dir);
    if !live.is_file() {
        anyhow::bail!(
            "no live EEPROM at {} — run the sketch and complete the setup flow first",
            live.display()
        );
    }
    if let Some(parent) = dest.parent() {
        std::fs::create_dir_all(parent)
            .with_context(|| format!("create parent dir for {}", dest.display()))?;
    }
    std::fs::copy(&live, dest)
        .with_context(|| format!("copy {} → {}", live.display(), dest.display()))?;
    Ok(())
}

/// Copies `src` over the live EEPROM (creating parent dirs). Always
/// overwrites; the caller decided this is the right state to restore.
pub fn restore(project_dir: &Path, src: &Path) -> Result<()> {
    if !src.is_file() {
        anyhow::bail!("seed file {} does not exist", src.display());
    }
    let live = live_path(project_dir);
    if let Some(parent) = live.parent() {
        std::fs::create_dir_all(parent)
            .with_context(|| format!("create parent dir for {}", live.display()))?;
    }
    std::fs::copy(src, &live)
        .with_context(|| format!("copy {} → {}", src.display(), live.display()))?;
    Ok(())
}

/// At launch: if no live EEPROM exists but a default seed does, restore from
/// the seed so the sketch boots in its configured post-setup state. Returns
/// `Ok(Some(path))` describing the seed used, `Ok(None)` if no action taken.
/// Never overwrites an existing live EEPROM — that represents in-progress
/// runtime state the user may not want clobbered.
pub fn auto_restore_if_missing(project_dir: &Path) -> Result<Option<PathBuf>> {
    let live = live_path(project_dir);
    if live.is_file() {
        return Ok(None);
    }
    let seed = default_seed_path(project_dir);
    if !seed.is_file() {
        return Ok(None);
    }
    restore(project_dir, &seed)?;
    Ok(Some(seed))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp() -> tempfile::TempDir {
        tempfile::tempdir().expect("tmpdir")
    }

    #[test]
    fn save_then_restore_roundtrip() {
        let proj = tmp();
        // Pretend the sketch wrote a live EEPROM.
        let live = live_path(proj.path());
        std::fs::create_dir_all(live.parent().unwrap()).unwrap();
        std::fs::write(&live, b"\x2B\x02\x00\x00configured-state").unwrap();

        let seed_dest = proj.path().join("seeds").join("snap.bin");
        save(proj.path(), &seed_dest).unwrap();
        assert_eq!(std::fs::read(&seed_dest).unwrap(), b"\x2B\x02\x00\x00configured-state");

        // Wipe live, restore from snap.
        std::fs::remove_file(&live).unwrap();
        restore(proj.path(), &seed_dest).unwrap();
        assert_eq!(std::fs::read(&live).unwrap(), b"\x2B\x02\x00\x00configured-state");
    }

    #[test]
    fn save_errors_when_no_live_eeprom() {
        let proj = tmp();
        let err = save(proj.path(), &proj.path().join("snap.bin")).unwrap_err();
        assert!(err.to_string().contains("no live EEPROM"));
    }

    #[test]
    fn auto_restore_uses_default_seed_when_live_missing() {
        let proj = tmp();
        std::fs::write(default_seed_path(proj.path()), b"seeded").unwrap();
        let used = auto_restore_if_missing(proj.path()).unwrap();
        assert_eq!(used.unwrap(), default_seed_path(proj.path()));
        assert_eq!(std::fs::read(live_path(proj.path())).unwrap(), b"seeded");
    }

    #[test]
    fn auto_restore_skips_when_live_already_exists() {
        let proj = tmp();
        std::fs::create_dir_all(live_path(proj.path()).parent().unwrap()).unwrap();
        std::fs::write(live_path(proj.path()), b"in-progress").unwrap();
        std::fs::write(default_seed_path(proj.path()), b"seeded").unwrap();
        let used = auto_restore_if_missing(proj.path()).unwrap();
        assert!(used.is_none());
        // Live untouched.
        assert_eq!(std::fs::read(live_path(proj.path())).unwrap(), b"in-progress");
    }

    #[test]
    fn auto_restore_noops_when_no_seed_present() {
        let proj = tmp();
        let used = auto_restore_if_missing(proj.path()).unwrap();
        assert!(used.is_none());
        assert!(!live_path(proj.path()).exists());
    }
}
