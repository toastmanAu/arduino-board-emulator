use anyhow::{Context, Result};
use std::path::{Path, PathBuf};

/// Recursively copies `src` to `dest`, applying selective content overrides.
///
/// `overrides` is a slice of (path-relative-to-src, new-contents) pairs. Any
/// file in `src` whose canonical relative path matches an override entry is
/// written with the override contents instead of being copied verbatim.
///
/// Pre-existing contents at `dest` are cleared first to keep the mirror
/// deterministic.
pub fn mirror(
    src: &Path,
    dest: &Path,
    overrides: &[(PathBuf, String)],
) -> Result<()> {
    if dest.exists() {
        std::fs::remove_dir_all(dest)
            .with_context(|| format!("clear mirror dest {:?}", dest))?;
    }
    std::fs::create_dir_all(dest)
        .with_context(|| format!("create mirror dest {:?}", dest))?;
    walk(src, src, dest, overrides)
}

fn walk(
    root: &Path,
    cur_src: &Path,
    cur_dest: &Path,
    overrides: &[(PathBuf, String)],
) -> Result<()> {
    for entry in std::fs::read_dir(cur_src)
        .with_context(|| format!("read_dir {:?}", cur_src))?
    {
        let entry = entry?;
        let path = entry.path();
        let name = entry.file_name();
        let dest_path = cur_dest.join(&name);

        if path.is_dir() {
            // Skip .boardghost so we don't recurse into our own build cache.
            if name == ".boardghost" { continue; }
            std::fs::create_dir_all(&dest_path)?;
            walk(root, &path, &dest_path, overrides)?;
        } else if path.is_file() {
            let rel = path.strip_prefix(root).unwrap_or(&path).to_path_buf();
            if let Some((_, contents)) = overrides.iter().find(|(p, _)| p == &rel) {
                std::fs::write(&dest_path, contents)
                    .with_context(|| format!("write override {:?}", dest_path))?;
            } else {
                std::fs::copy(&path, &dest_path)
                    .with_context(|| format!("copy {:?} -> {:?}", path, dest_path))?;
            }
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
    fn mirrors_a_flat_dir() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        fs::write(src.path().join("a.txt"), "alpha").unwrap();
        fs::write(src.path().join("b.txt"), "beta").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert_eq!(fs::read_to_string(dest_dir.join("a.txt")).unwrap(), "alpha");
        assert_eq!(fs::read_to_string(dest_dir.join("b.txt")).unwrap(), "beta");
    }

    #[test]
    fn applies_override_for_matched_file() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        fs::write(src.path().join("setup.h"), "ORIGINAL").unwrap();
        let overrides = vec![(PathBuf::from("setup.h"), "REPLACED".into())];
        mirror(src.path(), &dest_dir, &overrides).unwrap();
        assert_eq!(fs::read_to_string(dest_dir.join("setup.h")).unwrap(), "REPLACED");
    }

    #[test]
    fn recurses_into_subdirs() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        let sub = src.path().join("data");
        fs::create_dir(&sub).unwrap();
        fs::write(sub.join("payload.bin"), b"abc").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert_eq!(fs::read(dest_dir.join("data/payload.bin")).unwrap(), b"abc");
    }

    #[test]
    fn skips_dot_boardghost() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        let bg = src.path().join(".boardghost");
        fs::create_dir(&bg).unwrap();
        fs::write(bg.join("stale.txt"), "leftover").unwrap();
        fs::write(src.path().join("real.ino"), "void setup(){}").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert!(!dest_dir.join(".boardghost").exists());
        assert!(dest_dir.join("real.ino").exists());
    }

    #[test]
    fn clears_existing_dest() {
        let src = tempdir().unwrap();
        let dest = tempdir().unwrap();
        let dest_dir = dest.path().join("mirror");
        fs::create_dir_all(&dest_dir).unwrap();
        fs::write(dest_dir.join("stale.txt"), "old").unwrap();
        fs::write(src.path().join("new.txt"), "new").unwrap();
        mirror(src.path(), &dest_dir, &[]).unwrap();
        assert!(!dest_dir.join("stale.txt").exists());
        assert!(dest_dir.join("new.txt").exists());
    }
}
