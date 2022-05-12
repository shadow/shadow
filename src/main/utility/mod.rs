// defines macros, so must be included first
#[macro_use]
pub mod enum_passthrough;

pub mod byte_queue;
pub mod childpid_watcher;
pub mod counter;
pub mod event_queue;
pub mod give;
pub mod interval_map;
pub mod notnull;
pub mod pcap_writer;
pub mod perf_timer;
pub mod pod;
pub mod proc_maps;
pub mod random;
pub mod status_bar;
pub mod stream_len;
pub mod syscall;
pub mod time;

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

/// A trait we can use as a compile-time check to make sure that an object is Send.
pub trait IsSend: Send {}

/// A trait we can use as a compile-time check to make sure that an object is Sync.
pub trait IsSync: Sync {}

/// Runtime memory error checking to help catch errors that C code is prone
/// to. Can probably drop once C interop is removed.
///
/// Prefer to place `Magic` struct fields as the *first* field.  This causes the
/// `Magic` field to be dropped first when dropping the enclosing struct, which
/// validates that the `Magic` is valid before running `Drop` implementations of
/// the other fields.
///
/// The MAGIC parameter should ideally be unique for each type. Consider e.g.
/// `python3 -c 'import random; print(random.randint(0, 2**32))'`
#[derive(Debug)]
pub struct Magic<const MAGIC: u32> {
    #[cfg(debug_assertions)]
    magic: u32,
}

impl<const MAGIC: u32> Magic<MAGIC> {
    pub fn new() -> Self {
        Self {
            #[cfg(debug_assertions)]
            magic: MAGIC,
        }
    }

    pub fn debug_check(&self) {
        #[cfg(debug_assertions)]
        {
            if unsafe { std::ptr::read_volatile(&self.magic) } != MAGIC {
                // Do not pass Go; do not collect $200... and do not run Drop
                // implementations etc. after learning that Rust's soundness
                // requirements have likely been violated.
                std::process::abort();
            }
            // Ensure no other operations are performed on the object before validating.
            std::sync::atomic::compiler_fence(std::sync::atomic::Ordering::SeqCst);
        }
    }
}

impl<const MAGIC: u32> Drop for Magic<MAGIC> {
    fn drop(&mut self) {
        self.debug_check();
        #[cfg(debug_assertions)]
        unsafe {
            std::ptr::write_volatile(&mut self.magic, 0)
        };
    }
}

impl<const MAGIC: u32> Clone for Magic<MAGIC> {
    fn clone(&self) -> Self {
        self.debug_check();
        Self {
            #[cfg(debug_assertions)]
            magic: self.magic.clone(),
        }
    }
}
