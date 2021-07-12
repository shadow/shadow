pub mod byte_queue;
pub mod childpid_watcher;
pub mod counter;
pub mod event_queue;
pub mod interval_map;
pub mod notnull;
pub mod perf_timer;
pub mod pod;
pub mod proc_maps;
pub mod stream_len;
pub mod syscall;

/// A type that allows us to make a pointer Send + Sync since there is no way
/// to add these traits to the pointer itself.
#[derive(Clone, Copy, Debug)]
pub struct SyncSendPointer<T>(pub *mut T);

unsafe impl<T> Send for SyncSendPointer<T> {}
unsafe impl<T> Sync for SyncSendPointer<T> {}

impl<T> SyncSendPointer<T> {
    /// Get the pointer.
    pub fn ptr(&self) -> *mut T {
        self.0
    }

    /// Get a mutable reference to the pointer.
    pub fn ptr_ref(&mut self) -> &mut *mut T {
        &mut self.0
    }
}
