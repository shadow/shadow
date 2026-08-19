use std::{
    collections::{BTreeMap, BTreeSet},
    ops::Range,
};

use linux_api::fcntl::FlockType;
use rangemap::RangeMap;

use crate::host::process::ProcessId;

/// Owner of a record lock.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Ord, PartialOrd)]
pub enum LockOwner {
    // For "process-associated" (posix) locks, as created via `fcntl` operation
    // `F_SETLK`.
    Process(ProcessId),
    // TODO: add enumerator for "open file description" locks, as created via
    // `fcntl` operation `F_OFD_SETLK`. We'll need some appropriate identifier
    // to do so.
}

/// Internal representation of a record lock.
#[derive(Clone, Debug, Eq, PartialEq)]
enum FcntlLock {
    // Unlocked.
    None,
    // Write-lock; one owner.
    Write(LockOwner),
    // Read-lock; multiple owners.
    // Important to use a sorted set here, so that an "arbitrary" conflicting
    // lock returned via a.g. `F_GETLK` is deterministic.
    Read(BTreeSet<LockOwner>),
}

impl FcntlLock {
    fn new(requester: LockOwner, flock_type: FlockType) -> FcntlLock {
        match flock_type {
            FlockType::F_UNLCK => FcntlLock::None,
            FlockType::F_RDLCK => FcntlLock::Read(BTreeSet::from_iter([requester])),
            FlockType::F_WRLCK => FcntlLock::Write(requester),
        }
    }

    fn access(&self) -> FlockType {
        match self {
            FcntlLock::None => FlockType::F_UNLCK,
            FcntlLock::Write(_) => FlockType::F_WRLCK,
            FcntlLock::Read(_) => FlockType::F_RDLCK,
        }
    }

    fn owners_contains(&self, o: LockOwner) -> bool {
        match self {
            FcntlLock::None => false,
            FcntlLock::Write(write_owner) => write_owner == &o,
            FcntlLock::Read(read_owners) => read_owners.contains(&o),
        }
    }

    /// If the requested lock type is compatible with this existing lock type, return the
    /// result of merging the two. Otherwise returns an error that describes one of the
    /// conflicting locks.
    ///
    /// Use `requested_access=None` to unlock.
    ///
    /// Returns:
    /// * `Ok(None)` when the result is "no lock"; i.e. when
    ///   `requested_access` is `None`, and `requested_owner` is the only owner of
    ///   `self`.
    /// * `Ok(Some(x))`, where is the result of a successful merge.
    /// * `Err(x)`, where x is one of the current owners of a conflicting lock.
    pub fn merged_with(
        &self,
        requester: LockOwner,
        requested_type: FlockType,
    ) -> Result<Self, LockOwner> {
        match self {
            FcntlLock::None => {
                // Unlocked -> requested lock
                Ok(Self::new(requester, requested_type))
            }
            FcntlLock::Write(current_owner) => {
                if current_owner == &requester {
                    // requester is already the exclusive owner.
                    // Give them whatever they want.
                    Ok(Self::new(requester, requested_type))
                } else if requested_type == FlockType::F_UNLCK {
                    // requester is releasing, but isn't an owner of this lock.
                    // Return this lock unchanged.
                    Ok(self.clone())
                } else {
                    // Conflict with current owner.
                    Err(*current_owner)
                }
            }
            FcntlLock::Read(current_owners) => {
                match requested_type {
                    FlockType::F_RDLCK => {
                        // Add requester to the set of read-lock owners
                        let mut s = current_owners.clone();
                        s.insert(requester);
                        Ok(FcntlLock::Read(s))
                    }
                    FlockType::F_WRLCK => {
                        match current_owners.iter().find(|o| o != &&requester) {
                            Some(conflicting_owner) => {
                                // requesting a write lock, but a different owner
                                // is holding a read lock.
                                Err(*conflicting_owner)
                            }
                            None => {
                                // requesting a write lock, and requester is the sole holder
                                // of a read lock. Upgrade it to a write lock.
                                Ok(FcntlLock::new(requester, FlockType::F_WRLCK))
                            }
                        }
                    }
                    FlockType::F_UNLCK => {
                        // Requesting to unlock, and there's a read-lock.
                        // Remove requester from set of read-lock owners.
                        let new_owners = BTreeSet::from_iter(
                            current_owners.iter().filter(|x| x != &&requester).copied(),
                        );
                        if new_owners.is_empty() {
                            // No owners left -> unlocked.
                            Ok(FcntlLock::None)
                        } else {
                            Ok(FcntlLock::Read(new_owners))
                        }
                    }
                }
            }
        }
    }
}

/// Stable identifier for a file, which we use to look up record locks.
/// Remains valid as long as the file has links in the file system and/or there
/// are open file descriptors to the file.
//
// We implement this using device+inode, as returned by `fstat`.
//
// According to
// [posix](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_stat.h.html):
//
//   A file identity is uniquely determined by the combination of st_dev and
//   st_ino. At any given time in a system, distinct files shall have distinct
//   file identities; hard links to the same file shall have the same file
//   identity. Over time, these file identities can be reused for different files.
//   For example, the st_ino value can be reused after the last link to a file is
//   unlinked and the space occupied by the file has been freed, and the st_dev
//   value associated with a file system can be reused if that file system is
//   detached ("unmounted") and another is attached ("mounted").
//
// While the posix definition doesn't seem to clearly specify whether an inode
// number can be reused when there are no more links in the file system, but the
// file is still open, my understanding is that at least in Linux, files aren't
// destroyed and their inode numbers made available for reuse until it is no
// longer open by anyone. e.g.
// [unlink(2)](https://man7.org/linux/man-pages/man2/unlink.2.html):
//
//   If the name was the last link to a file but any processes still
//   have the file open, the file will remain in existence until the
//   last file descriptor referring to it is closed.
#[derive(Debug, Copy, Clone, Eq, PartialEq, Ord, PartialOrd)]
pub struct FileId {
    device: u64,
    inode: u64,
}

impl From<&linux_api::stat::stat> for FileId {
    fn from(value: &linux_api::stat::stat) -> Self {
        FileId {
            device: value.lst_dev,
            inode: value.lst_ino,
        }
    }
}

impl From<&libc::stat> for FileId {
    fn from(value: &libc::stat) -> Self {
        FileId {
            device: value.st_dev,
            inode: value.st_ino,
        }
    }
}

#[derive(Debug)]
struct FcntlLockTableForOneFile {
    locks: RangeMap<usize, FcntlLock>,
    // Will need something here to track sleepers
}

impl FcntlLockTableForOneFile {
    fn new() -> Self {
        let mut locks = RangeMap::new();
        // All unlocked.
        locks.insert(usize::MIN..usize::MAX, FcntlLock::None);
        Self { locks }
    }

    pub fn is_empty(&self) -> bool {
        self.locks.is_empty()
    }

    /// Apply the requested lock to the given range. Similar semantics as
    /// `fcntl` operations like `F_SETLK`.
    ///
    /// Returns an error if an incompatible lock exists.
    pub fn set_lock(
        &mut self,
        requested_range: Range<usize>,
        requester: LockOwner,
        requested_type: FlockType,
    ) -> Result<(), ()> {
        // Process overlapping locks, collecting what the merged locks will look like.
        let mut merge_results = Vec::<(Range<usize>, FcntlLock)>::new();
        for (lock_range, lock) in self.locks.overlapping(&requested_range) {
            match lock.merged_with(requester, requested_type) {
                Ok(merged_lock) => {
                    // Record the merged lock.
                    let start = std::cmp::max(lock_range.start, requested_range.start);
                    let end = std::cmp::min(lock_range.end, requested_range.end);
                    merge_results.push((start..end, merged_lock));
                }
                Err(_) => {
                    // Conflicting lock; can't merge.
                    return Err(());
                }
            }
        }

        // Insert the results.
        for (merged_range, merged_lock) in merge_results {
            self.locks.insert(merged_range, merged_lock);
        }

        Ok(())
    }

    /// Return one of the locks that would conflict with the requested lock, if
    /// any. The return range is *uncoalesced*, meaning that if a conflicting read lock
    /// is returned, the bounds have not been expanded to include adjancent read locks
    /// that also have the returned owner.
    ///
    /// Intended for `fcntl` operations like `F_GETLK`, but result is the raw
    /// internal lock, if any.
    fn get_uncoalesced_conflicting_lock(
        &self,
        requested_range: Range<usize>,
        requester: LockOwner,
        requested_type: FlockType,
    ) -> Option<(LockOwner, Range<usize>, FlockType)> {
        for (lock_range, lock) in self.locks.overlapping(&requested_range) {
            match lock.merged_with(requester, requested_type) {
                Ok(_) => (), // No conflict,
                Err(o) => return Some((o, lock_range.clone(), lock.access())),
            }
        }
        None
    }

    /// Return one of the locks that would conflict with the requested lock, if
    /// any. Intended for `fcntl` operations like `F_GETLK`.
    ///
    /// The returned range is coalesced for the returned owner: it includes all adjacent
    /// range over which the owner has the returned access, even if parts of the range
    /// have different ownership sets. (Reproducing the behavior of `F_GETLK` on Linux).
    pub fn get_coalesced_conflicting_lock(
        &self,
        requested_range: Range<usize>,
        requester: LockOwner,
        requested_type: FlockType,
    ) -> Option<(LockOwner, Range<usize>, FlockType)> {
        let (conflicting_owner, mut conflicting_range, conflicting_access) =
            self.get_uncoalesced_conflicting_lock(requested_range, requester, requested_type)?;

        let can_coalesce = |other_lock: &FcntlLock| -> bool {
            other_lock.access() == conflicting_access
                && other_lock.owners_contains(conflicting_owner)
        };

        // Coalesce backwards
        while let Some(x) = conflicting_range.start.checked_sub(1) {
            let Some((prev_range, prev_lock)) = self.locks.get_key_value(&x) else {
                break;
            };
            if !(can_coalesce(prev_lock)) {
                break;
            }
            conflicting_range.start = prev_range.start
        }

        // Coalesce forwards
        while let Some(x) = conflicting_range.end.checked_add(1) {
            let Some((next_range, next_lock)) = self.locks.get_key_value(&x) else {
                break;
            };
            if !(can_coalesce(next_lock)) {
                break;
            }
            conflicting_range.end = next_range.end
        }

        Some((conflicting_owner, conflicting_range, conflicting_access))
    }

    /// Drop all of the given owner's locks on the given file.
    pub fn remove_owner(&mut self, requester: LockOwner) {
        // Modified locks with `requester` removed, which we'll need to re-insert.
        let mut to_insert = Vec::new();
        for (range, lock) in self.locks.iter() {
            match lock {
                FcntlLock::None => {
                    // Nothing to remove from; no change needed.
                }
                FcntlLock::Write(lock_owner) => {
                    if lock_owner == &requester {
                        // `requester` was the sole owner; clear the lock.
                        to_insert.push((range.clone(), FcntlLock::None))
                    } else {
                        // not an owner; no change needed.
                    }
                }
                FcntlLock::Read(owners) => {
                    if owners.contains(&requester) {
                        if owners.len() == 1 {
                            // `requester` was the sole owner; clear the lock.
                            to_insert.push((range.clone(), FcntlLock::None))
                        } else {
                            // `requester` was a shared owner; overwrite
                            // with a lock that doesn't include them.
                            let mut updated_owners = owners.clone();
                            updated_owners.remove(&requester);
                            to_insert.push((range.clone(), FcntlLock::Read(updated_owners)))
                        }
                    }
                }
            }
        }
        for (range, lock) in to_insert {
            log::trace!("removing owner {requester:?} from {range:?} (resulting in {lock:?})");
            self.locks.insert(range, lock);
        }
    }
}

/// All record locks for a Host.
#[derive(Debug)]
pub struct FcntlLockTable {
    /// locks by FileId.
    locks: BTreeMap<FileId, FcntlLockTableForOneFile>,
}

impl FcntlLockTable {
    pub fn new() -> Self {
        Self {
            locks: BTreeMap::new(),
        }
    }

    #[allow(clippy::result_unit_err)]
    pub fn set_lock(
        &mut self,
        file_id: FileId,
        requested_range: Range<usize>,
        requested_owner: LockOwner,
        requested_access: FlockType,
    ) -> Result<(), ()> {
        let file_locks = self
            .locks
            .entry(file_id)
            .or_insert_with(FcntlLockTableForOneFile::new);
        let res = file_locks.set_lock(requested_range.clone(), requested_owner, requested_access);
        if res.is_err() {
            log::debug!(
                "failed to lock {file_id:?}.{requested_range:?}.{requested_owner:?}.{requested_access:?}"
            );
        }
        if file_locks.is_empty() {
            self.locks.remove(&file_id);
        }
        res
    }

    /// Return one of the locks that would conflict with the requested lock, if
    /// any. Intended for `fcntl` operations like `F_GETLK`.
    pub fn get_coalesced_conflicting_lock(
        &self,
        file_id: FileId,
        requested_range: Range<usize>,
        requested_owner: LockOwner,
        requested_access: FlockType,
    ) -> Option<(LockOwner, Range<usize>, FlockType)> {
        let file_locks = self.locks.get(&file_id)?;
        let start = requested_range.start;
        let end = requested_range.end;
        let res = file_locks.get_coalesced_conflicting_lock(
            requested_range,
            requested_owner,
            requested_access,
        );
        log::trace!(
            "get_coalesced_conflicting_lock({file_locks:?}, {file_id:?}, {start}..{end}, {requested_owner:?}, {requested_access:?} -> {res:?})"
        );
        res
    }

    /// Drop all of the given owner's locks on the given file.
    pub fn remove_owner(&mut self, file_id: FileId, requested_owner: LockOwner) {
        let Some(file_locks) = self.locks.get_mut(&file_id) else {
            return;
        };
        file_locks.remove_owner(requested_owner);
    }
}

impl Default for FcntlLockTable {
    fn default() -> Self {
        Self::new()
    }
}
