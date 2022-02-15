use std::collections::HashSet;
use std::fs;
use std::path::Path;
use std::path::PathBuf;
use std::str::FromStr;

const PROC_DIR_PATH: &str = "/proc/";
const SHM_DIR_PATH: &str = "/dev/shm/";

// Get the paths from the given directory path.
// Logs a warning and returns None when an error occurs.
fn get_dir_contents(dir: &Path) -> Option<Vec<PathBuf>> {
    let mut paths = Vec::new();

    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(e) => {
            log::warn!(
                "Unable to read all directory entries from {:?}: {:?}",
                dir,
                e
            );
            return None;
        }
    };

    for entry in entries {
        let entry = match entry {
            Ok(entry) => entry,
            Err(e) => {
                log::warn!("Unable to read a directory entry from {:?}: {:?}", dir, e);
                return None;
            }
        };

        paths.push(entry.path());
    }

    Some(paths)
}

// Parse files in /dev/shm/ and return the paths for shmem files created by Shadow.
// Logs a warning and returns None when an error occurs.
fn get_shadow_shm_file_paths(dir_path: &Path) -> Option<Vec<PathBuf>> {
    let mut paths = Vec::new();

    for path in get_dir_contents(dir_path)? {
        // The file name is only None if the path ends in .., which we can ignore.
        if let Some(file_name) = path.file_name() {
            let name = file_name.to_string_lossy();
            if name.starts_with("shadow_shmemfile") {
                paths.push(path);
            } else {
                log::trace!("Skipping non-shadow shared memory file {:?}", path);
            }
        }
    }

    Some(paths)
}

// Parse PIDs from entries in /proc/.
// Logs a message at trace level and returns None when an error occurs.
fn get_running_pid_set(dir_path: &Path) -> Option<HashSet<i32>> {
    let mut pids = HashSet::new();

    for path in get_dir_contents(dir_path)? {
        // The file name is only None if the path ends in .., which we can ignore.
        if let Some(file_name) = path.file_name() {
            let name_str: &str = &file_name.to_string_lossy();
            match i32::from_str(name_str) {
                Ok(pid) => {
                    pids.insert(pid);
                }
                Err(e) => {
                    log::trace!("Skipping unparseable file name {}: {:?}", name_str, e);
                }
            };
        }
    }

    Some(pids)
}

// Parse the PID that is encoded in the Shadow shmem file name. The PID is the
// part after the '-', e.g., 2738869 in the example file name:
// `shadow_shmemfile_6379761.950298775-2738869`
// Logs a warning and returns None when an error occurs.
fn pid_from_shadow_shm_file(path: &PathBuf) -> Option<i32> {
    // The file name is only None if the path ends in .., which we can ignore.
    if let Some(file_name) = path.file_name() {
        let name_str: &str = &file_name.to_string_lossy();
        let pid_str = match name_str.split("-").last() {
            Some(s) => s,
            None => {
                log::warn!("Unable to parse PID separator '-' from shm file {:?}", path);
                return None;
            }
        };

        match i32::from_str(&pid_str) {
            Ok(pid) => {
                return Some(pid);
            }
            Err(e) => {
                log::warn!(
                    "Error while parsing PID '{}' from shm file {:?}: {:?}",
                    pid_str,
                    path,
                    e
                );
                return None;
            }
        }
    }
    None
}

// Cleans up orphaned shared memory files that are no longer mapped by a shadow
// process. This function should never fail or crash, but is not guaranteed to
// reclaim all possible orphans. Returns the number of orphans removed.
pub fn try_shm_cleanup() -> u32 {
    // Get the shm file paths before the PIDs to avoid a race condition (#1343).
    let shm_paths = match get_shadow_shm_file_paths(&Path::new(SHM_DIR_PATH)) {
        Some(paths) => {
            log::debug!(
                "Found {} shadow shared memory files in {}",
                paths.len(),
                SHM_DIR_PATH
            );
            paths
        }
        None => {
            log::warn!(
                "Unable to get shadow shared memory file paths from {}; skipping cleanup",
                SHM_DIR_PATH
            );
            return 0;
        }
    };

    let running_pids = match get_running_pid_set(&Path::new(PROC_DIR_PATH)) {
        Some(pids) => {
            log::debug!("Found {} running PIDs in {}", pids.len(), PROC_DIR_PATH);
            pids
        }
        None => {
            log::warn!(
                "Unable to get running PIDs from {}; skipping cleanup",
                PROC_DIR_PATH
            );
            return 0;
        }
    };

    // Count how many files we remove.
    let mut num_removed = 0;

    // Best effort: ignore failures on individual paths so we can try them all.
    for path in shm_paths {
        if let Some(creator_pid) = pid_from_shadow_shm_file(&path) {
            if !running_pids.contains(&creator_pid) {
                if let Ok(_) = fs::remove_file(path) {
                    log::debug!("Removed orphaned shared memory file");
                    num_removed += 1;
                }
            }
        }
    }

    log::debug!("Removed {} total shared memory files.", num_removed);
    num_removed
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::OpenOptions;
    use std::process;
    use std::{fs, io};

    fn touch(path: &Path) -> io::Result<()> {
        match OpenOptions::new().create(true).write(true).open(path) {
            Ok(_) => Ok(()),
            Err(e) => Err(e),
        }
    }

    #[test]
    fn test_expired_shm_file_is_removed() {
        let s = "/dev/shm/shadow_shmemfile_6379761.950298775-999999999";
        let expired = Path::new(s);

        touch(&expired).unwrap();
        try_shm_cleanup();
        assert!(!expired.exists());
    }

    #[test]
    fn test_valid_shm_file_is_not_removed() {
        let my_pid = process::id();
        let s = format!("/dev/shm/shadow_shmemfile_6379761.950298775-{my_pid}");
        let valid = Path::new(&s);

        touch(&valid).unwrap();
        try_shm_cleanup();
        assert!(valid.exists());

        fs::remove_file(valid).unwrap();
    }

    #[test]
    fn test_nonshadow_shm_file_is_not_removed() {
        let s = "/dev/shm/shadow_unimportant_test_file";
        let nonshadow = Path::new(s);

        touch(&nonshadow).unwrap();
        try_shm_cleanup();
        assert!(nonshadow.exists());

        fs::remove_file(nonshadow).unwrap();
    }
}
