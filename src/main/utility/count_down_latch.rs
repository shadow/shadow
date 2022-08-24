use std::sync::Barrier;

pub struct CountDownLatch {
    barrier: Barrier,
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn countdownlatch_new(count: usize) -> *mut CountDownLatch {
        Box::into_raw(Box::new(CountDownLatch {
            barrier: Barrier::new(count),
        }))
    }

    #[no_mangle]
    pub extern "C" fn countdownlatch_free(latch: *mut CountDownLatch) {
        assert!(!latch.is_null());
        unsafe { Box::from_raw(latch) };
    }

    #[no_mangle]
    pub extern "C" fn countdownlatch_wait(latch: *const CountDownLatch) {
        let latch = unsafe { latch.as_ref() }.unwrap();
        latch.barrier.wait();
    }
}
