use std::cmp::Ordering;

use crate::host::descriptor::File;

/// A `Key` helps us find an epoll entry given the fd and `File` object available at the time that a
/// syscall is made. Epoll uses `Key`s to be able to add the same `File` multiple times under
/// different fds, and add the same fd multiple times as long as the `File` is different.
#[derive(Clone, Eq, Ord, PartialEq, PartialOrd)]
pub(super) struct Key {
    fd: i32,
    file: File,
}

impl Key {
    pub(super) fn new(fd: i32, file: File) -> Self {
        Self { fd, file }
    }

    pub(super) fn get_fd(&self) -> i32 {
        self.fd
    }

    pub(super) fn get_file_ref(&self) -> &File {
        &self.file
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
    pub(super) fn new(pri: u64, key: Key) -> Self {
        Self { pri, key }
    }
}

impl Eq for PriorityKey {}

impl PartialEq for PriorityKey {
    fn eq(&self, other: &Self) -> bool {
        self.key.eq(&other.key)
    }
}

impl Ord for PriorityKey {
    fn cmp(&self, other: &Self) -> Ordering {
        self.pri
            .cmp(&other.pri)
            .then_with(|| self.key.cmp(&other.key))
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
