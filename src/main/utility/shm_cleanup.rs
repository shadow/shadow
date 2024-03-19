use std::collections::HashSet;
use std::fs;
use std::path::Path;
use std::path::PathBuf;
use std::str::FromStr;

use anyhow::Context;

pub const SHM_DIR_PATH: &str = "/dev/shm/";
const PROC_DIR_PATH: &str = "/proc/";
const SHADOW_SHM_FILE_PREFIX: &str = "shadow_shmemfile";

// Get the paths from the given directory path.
fn get_dir_contents(dir: &Path) -> anyhow::Result<Vec<PathBuf>> {
    fs::read_dir(dir)
        .context(format!("Reading all directory entries from {:?}", dir))?
        .map(|entry| {
            Ok(entry
                .context(format!("Reading a directory entry from {:?}", dir))?
                .path())
        })
        .collect()
}

// Parse files in dir_path and return the paths to the shm files created by Shadow.
fn get_shadow_shm_file_paths(dir_path: &Path) -> anyhow::Result<Vec<PathBuf>> {
    let vec = get_dir_contents(dir_path)?
        .into_iter()
        .filter_map(|path| match path.file_name() {
            Some(name) => name
                .to_string_lossy()
                .starts_with(SHADOW_SHM_FILE_PREFIX)
                .then_some(Some(path)),
            None => None, // ignore paths ending in '..'
        })
        .flatten()
        .collect();
    Ok(vec)
}

// Parse PIDs from entries in dir_path.
fn get_running_pid_set(dir_path: &Path) -> anyhow::Result<HashSet<i32>> {
    let set: HashSet<i32> = get_dir_contents(dir_path)?
        .into_iter()
        .filter_map(|path| match path.file_name() {
            // ignore names that don't parse into PIDs
            Some(name) => i32::from_str(&name.to_string_lossy()).ok(),
            None => None, // ignore paths ending in '..'
        })
        .collect();
    Ok(set)
}

// Parse the PID that is encoded in the Shadow shmem file name. The PID is the
// part after the '-', e.g., 2738869 in the example file name:
// `shadow_shmemfile_6379761.950298775-2738869`
fn pid_from_shadow_shm_file_name(file_name: &str) -> anyhow::Result<i32> {
    let pid_str = file_name.split('-').last().context(format!(
        "Parsing PID separator '-' from shm file name {:?}",
        file_name
    ))?;
    let pid = i32::from_str(pid_str).context(format!(
        "Parsing PID '{}' from shm file name {:?}",
        pid_str, file_name
    ))?;
    Ok(pid)
}

// Cleans up orphaned shared memory files that are no longer mapped by a shadow
// process. This function should never fail or crash, but is not guaranteed to
// reclaim all possible orphans. Returns the number of orphans removed.
pub fn shm_cleanup(shm_dir: impl AsRef<Path>) -> anyhow::Result<u32> {
    // Get the shm file paths before the PIDs to avoid a race condition (#1343).
    let shm_paths = get_shadow_shm_file_paths(shm_dir.as_ref())?;
    log::debug!(
        "Found {} shadow shared memory files in {}",
        shm_paths.len(),
        shm_dir.as_ref().display()
    );

    let running_pids = get_running_pid_set(Path::new(PROC_DIR_PATH))?;
    log::debug!(
        "Found {} running PIDs in {}",
        running_pids.len(),
        PROC_DIR_PATH
    );

    // Count how many files we remove.
    let mut num_removed = 0;

    // Best effort: ignore failures on individual paths so we can try them all.
    for path in shm_paths {
        // Ignore paths ending in '..'
        if let Some(file_name) = path.file_name() {
            let creator_pid = match pid_from_shadow_shm_file_name(&file_name.to_string_lossy()) {
                Ok(pid) => pid,
                Err(e) => {
                    log::warn!(
                        "Unable to parse PID from shared memory file {:?}: {:?}",
                        path,
                        e
                    );
                    // Keep going to try the rest of the paths we found.
                    continue;
                }
            };

            // Do not remove the file if it's owner process is still running.
            if !running_pids.contains(&creator_pid) {
                log::trace!("Removing orphaned shared memory file {:?}", path);
                if fs::remove_file(path).is_ok() {
                    num_removed += 1;
                }
            }
        }
    }

    log::debug!("Removed {} total shared memory files.", num_removed);
    Ok(num_removed)
}

#[cfg(test)]
mod tests {
    use std::fs::OpenOptions;
    use std::io;
    use std::process;

    use super::*;

    fn touch(path: impl AsRef<Path>) -> io::Result<()> {
        OpenOptions::new()
            .create(true)
            .write(true)
            .open(path.as_ref())?;
        Ok(())
    }

    #[test]
    fn test_expired_shm_file_is_removed() {
        let dir = tempfile::tempdir().unwrap();
        let s = "shadow_shmemfile_6379761.950298775-999999999";
        let expired: PathBuf = [dir.as_ref(), s.as_ref()].iter().collect();

        touch(&expired).unwrap();
        assert_eq!(shm_cleanup(&dir).unwrap(), 1);
        assert!(!expired.exists(), "Exists: {}", expired.display());
    }

    #[test]
    fn test_valid_shm_file_is_not_removed() {
        let my_pid = process::id();
        let dir = tempfile::tempdir().unwrap();
        let s = format!("shadow_shmemfile_6379761.950298775-{my_pid}");
        let valid: PathBuf = [dir.as_ref(), s.as_ref()].iter().collect();

        touch(&valid).unwrap();
        assert_eq!(shm_cleanup(&dir).unwrap(), 0);
        assert!(valid.exists(), "Doesn't exist: {}", valid.display());
    }

    #[test]
    fn test_nonshadow_shm_file_is_not_removed() {
        let dir = tempfile::tempdir().unwrap();
        let s = "shadow_unimportant_test_file";
        let nonshadow: PathBuf = [dir.as_ref(), s.as_ref()].iter().collect();

        touch(&nonshadow).unwrap();
        assert_eq!(shm_cleanup(&dir).unwrap(), 0);
        assert!(nonshadow.exists(), "Doesn't exist: {}", nonshadow.display());
    }
}
