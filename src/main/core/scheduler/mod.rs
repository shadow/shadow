mod logical_processor;
pub mod runahead;
pub mod scheduler;
mod workpool;

use std::cell::RefCell;

// any scheduler implementation can read/write the thread-local directly, but external modules can
// only read it using `core_affinity()`

std::thread_local! {
    /// The core affinity of the current thread, as set by the active scheduler.
    static CORE_AFFINITY: RefCell<Option<u32>> = RefCell::new(None);
}

/// Get the core affinity of the current thread, as set by the active scheduler.
pub fn core_affinity() -> Option<u32> {
    CORE_AFFINITY.with(|x| *x.borrow())
}

mod export {
    use super::*;

    /// Get the core affinity of the current thread, as set by the active scheduler. Returns `-1` if
    /// the affinity is not set.
    #[no_mangle]
    pub extern "C" fn scheduler_getAffinity() -> i32 {
        core_affinity()
            .map(|x| i32::try_from(x).unwrap())
            .unwrap_or(-1)
    }
}
