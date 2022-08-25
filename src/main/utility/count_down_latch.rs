use std::sync::{Condvar, Mutex};

pub struct CountDownLatch {
    lock: Mutex<LatchState>,
    cond: Condvar,
    num_counts: usize,
}

struct LatchState {
    count: usize,
}

impl CountDownLatch {
    pub fn new(num_counts: usize) -> Self {
        Self {
            lock: Mutex::new(LatchState { count: num_counts }),
            cond: Condvar::new(),
            num_counts,
        }
    }

    pub fn reset(&self) {
        let mut lock = self.lock.lock().unwrap();
        assert_eq!(lock.count, 0);
        lock.count = self.num_counts;
    }

    pub fn wait(&self) {
        let lock = self.lock.lock().unwrap();
        let _ = self.cond.wait_while(lock, |x| x.count > 0).unwrap();
    }

    pub fn count_down(&self) {
        let count;
        {
            let mut lock = self.lock.lock().unwrap();
            lock.count = lock.count.checked_sub(1).unwrap();
            count = lock.count;
        }

        if count == 0 {
            self.cond.notify_all();
        }
    }
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn countdownlatch_new(count: usize) -> *mut CountDownLatch {
        Box::into_raw(Box::new(CountDownLatch::new(count)))
    }

    #[no_mangle]
    pub extern "C" fn countdownlatch_free(latch: *mut CountDownLatch) {
        assert!(!latch.is_null());
        unsafe { Box::from_raw(latch) };
    }

    #[no_mangle]
    pub extern "C" fn countdownlatch_reset(latch: *const CountDownLatch) {
        let latch = unsafe { latch.as_ref() }.unwrap();
        latch.reset();
    }

    #[no_mangle]
    pub extern "C" fn countdownlatch_await(latch: *const CountDownLatch) {
        let latch = unsafe { latch.as_ref() }.unwrap();
        latch.wait();
    }

    #[no_mangle]
    pub extern "C" fn countdownlatch_countDown(latch: *const CountDownLatch) {
        let latch = unsafe { latch.as_ref() }.unwrap();
        latch.count_down();
    }
}
