use core::hash::Hash;
use std::cmp::Ordering;

use crate::host::descriptor::File;

/// A `Key` helps us find an epoll entry given the fd and `File` object available at the time that a
/// syscall is made. Epoll uses `Key`s to be able to add the same `File` multiple times under
/// different fds, and add the same fd multiple times as long as the `File` is different.
#[derive(Clone)]
pub(super) struct Key {
    fd: i32,
    file: File,
}

impl Key {
    pub fn new(fd: i32, file: File) -> Self {
        Self { fd, file }
    }

    pub fn file(&self) -> &File {
        &self.file
    }
}

impl Eq for Key {}

impl PartialEq for Key {
    fn eq(&self, other: &Self) -> bool {
        self.fd == other.fd && self.file.canonical_handle() == other.file.canonical_handle()
    }
}

impl Hash for Key {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.fd.hash(state);
        self.file.canonical_handle().hash(state);
    }
}

/// A `PriorityKey` helps us sort keys by a given priority. We use monitonically increasing priority
/// values to ensure fairness when reporting events (so that we don't always report events from the
/// same entry first and starve other entries). Using unique priority values for every key also
/// ensure deterministic sorting.
pub(super) struct PriorityKey {
    pri: u64,
    key: Key,
}

impl PriorityKey {
    pub fn new(pri: u64, key: Key) -> Self {
        Self { pri, key }
    }
}

impl Eq for PriorityKey {}

impl PartialEq for PriorityKey {
    fn eq(&self, other: &Self) -> bool {
        self.pri.eq(&other.pri)
    }
}

impl Ord for PriorityKey {
    fn cmp(&self, other: &Self) -> Ordering {
        self.pri.cmp(&other.pri)
    }
}

impl PartialOrd for PriorityKey {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl From<PriorityKey> for Key {
    fn from(value: PriorityKey) -> Self {
        value.key
    }
}
