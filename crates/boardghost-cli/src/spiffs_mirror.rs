use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

/// Mirrors the sketch's `data/` directory into the sim's assets root so that
/// `SPIFFS.open("/foo.png")` (and `LittleFS.open(...)`) resolve at runtime.
///
/// The runtime's `assets_root()` is `<project>/.boardghost/sim-assets/`,
/// derived by stepping up two levels from the build cwd. Each filesystem mounts
/// a subdir of that root — `spiffs/`, `littlefs/`, `sd/`. We populate both the
/// `spiffs/` and `littlefs/` subdirs from `data/` so a sketch using either FS
/// API picks up the same assets without special configuration.
///
/// Returns `Ok(Some(MirrorReport))` if `data/` existed and was mirrored,
/// `Ok(None)` if the sketch has no `data/` dir.
pub fn mirror_sketch_data(
    sketch_dir: &Path,
    sim_assets_root: &Path,
) -> Result<Option<MirrorReport>> {
    let data_dir = sketch_dir.join("data");
    if !data_dir.is_dir() {
        return Ok(None);
    }

    let mut report = MirrorReport {
        source: data_dir.clone(),
        targets: Vec::new(),
        file_count: 0,
        total_bytes: 0,
    };

    for mount in ["spiffs", "littlefs"] {
        let target = sim_assets_root.join(mount);
        // Wipe stale state — keeps repeated builds deterministic when files are
        // removed from data/.
        if target.exists() {
            std::fs::remove_dir_all(&target)
                .with_context(|| format!("clear sim-assets mount {:?}", target))?;
        }
        std::fs::create_dir_all(&target)
            .with_context(|| format!("create sim-assets mount {:?}", target))?;
        let (files, bytes) = copy_tree(&data_dir, &target)?;
        report.file_count = files; // identical across mounts; record once
        report.total_bytes = bytes;
        report.targets.push(target);
    }

    Ok(Some(report))
}

pub struct MirrorReport {
    pub source: PathBuf,
    pub targets: Vec<PathBuf>,
    pub file_count: usize,
    pub total_bytes: u64,
}

fn copy_tree(src: &Path, dest: &Path) -> Result<(usize, u64)> {
    let mut files = 0usize;
    let mut bytes = 0u64;
    walk(src, dest, &mut files, &mut bytes)?;
    Ok((files, bytes))
}

fn walk(cur_src: &Path, cur_dest: &Path, files: &mut usize, bytes: &mut u64) -> Result<()> {
    for entry in std::fs::read_dir(cur_src)
        .with_context(|| format!("read_dir {:?}", cur_src))?
    {
        let entry = entry?;
        let path = entry.path();
        let name = entry.file_name();
        let dest_path = cur_dest.join(&name);

        if path.is_dir() {
            std::fs::create_dir_all(&dest_path)?;
            walk(&path, &dest_path, files, bytes)?;
        } else if path.is_file() {
            let copied = std::fs::copy(&path, &dest_path)
                .with_context(|| format!("copy {:?} -> {:?}", path, dest_path))?;
            *files += 1;
            *bytes += copied;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn returns_none_when_no_data_dir() {
        let sketch = tempdir().unwrap();
        let assets = tempdir().unwrap();
        let report = mirror_sketch_data(sketch.path(), assets.path()).unwrap();
        assert!(report.is_none());
    }

    #[test]
    fn mirrors_data_dir_to_both_mounts() {
        let sketch = tempdir().unwrap();
        let assets = tempdir().unwrap();
        let data = sketch.path().join("data");
        fs::create_dir(&data).unwrap();
        fs::write(data.join("hello.txt"), b"world").unwrap();

        let report = mirror_sketch_data(sketch.path(), assets.path()).unwrap().unwrap();
        assert_eq!(report.file_count, 1);
        assert_eq!(report.total_bytes, 5);
        assert_eq!(
            fs::read_to_string(assets.path().join("spiffs/hello.txt")).unwrap(),
            "world"
        );
        assert_eq!(
            fs::read_to_string(assets.path().join("littlefs/hello.txt")).unwrap(),
            "world"
        );
    }

    #[test]
    fn recurses_into_subdirs() {
        let sketch = tempdir().unwrap();
        let assets = tempdir().unwrap();
        let data = sketch.path().join("data");
        let sub = data.join("fonts");
        fs::create_dir_all(&sub).unwrap();
        fs::write(sub.join("font.vlw"), b"FONTDATA").unwrap();

        mirror_sketch_data(sketch.path(), assets.path()).unwrap().unwrap();
        assert_eq!(
            fs::read(assets.path().join("spiffs/fonts/font.vlw")).unwrap(),
            b"FONTDATA"
        );
    }

    #[test]
    fn clears_stale_targets() {
        let sketch = tempdir().unwrap();
        let assets = tempdir().unwrap();
        let data = sketch.path().join("data");
        fs::create_dir(&data).unwrap();
        fs::write(data.join("new.txt"), b"new").unwrap();

        let spiffs = assets.path().join("spiffs");
        fs::create_dir_all(&spiffs).unwrap();
        fs::write(spiffs.join("stale.txt"), b"old").unwrap();

        mirror_sketch_data(sketch.path(), assets.path()).unwrap().unwrap();
        assert!(!spiffs.join("stale.txt").exists());
        assert!(spiffs.join("new.txt").exists());
    }
}
