//! This file contains tests intended to be run using [loom]. See the
//! [crate-level documentation](crate) for details about running these under
//! loom.
//!
//! [loom]: <https://docs.rs/loom/latest/loom/>
use vasi_sync::scmutex::{SelfContainedMutex, SelfContainedMutexGuard};

mod sync;

mod scmutex_tests {
    use super::*;

    #[test]
    fn test_basic() {
        sync::model(|| {
            let mutex = SelfContainedMutex::new(0);
            let mut guard = mutex.lock();
            *guard += 1;
            assert_eq!(*guard, 1);
        })
    }

    #[test]
    fn test_reconnect() {
        sync::model(|| {
            let mutex = SelfContainedMutex::new(0);
            let mut guard = mutex.lock();
            *guard += 1;
            guard.disconnect();
            let mut guard = SelfContainedMutexGuard::reconnect(&mutex);
            assert_eq!(*guard, 1);
            *guard += 1;
        })
    }

    #[test]
    fn test_reconnect_from_other_thread() {
        sync::model(|| {
            let mutex = sync::Arc::new(SelfContainedMutex::new(0));

            {
                let mutex = mutex.clone();
                sync::thread::spawn(move || {
                    let mut guard = mutex.lock();
                    *guard += 1;
                    guard.disconnect();
                })
                .join()
                .unwrap();
            }

            let guard = SelfContainedMutexGuard::reconnect(&mutex);
            assert_eq!(*guard, 1);
        })
    }

    #[test]
    fn test_threads() {
        sync::model_with_max_preemptions(2, || {
            let mutex = sync::Arc::new(SelfContainedMutex::new(0));

            // We can only create up to one fewer than loom's MAX_THREADS, which is currently 4.
            // https://docs.rs/loom/latest/loom/#combinatorial-explosion-with-many-threads
            //
            // This should be enough under loom, anyway.
            #[cfg(loom)]
            let nthreads = loom::MAX_THREADS - 1;
            #[cfg(not(loom))]
            let nthreads = 100;

            let threads: Vec<_> = (0..nthreads)
                .map(|i| {
                    let mutex = mutex.clone();
                    sync::thread::spawn(move || {
                        if i % 2 == 0 {
                            let mut guard = mutex.lock();
                            // Try to get more execution orders outside of loom.
                            sync::rand_sleep();
                            *guard += 1;
                        } else {
                            // As above, but also exercise the disconnect path.
                            let guard = mutex.lock();
                            guard.disconnect();
                            sync::rand_sleep();
                            let mut guard = SelfContainedMutexGuard::reconnect(&mutex);
                            *guard += 1;
                        }
                    })
                })
                .collect();

            for thread in threads {
                thread.join().unwrap();
            }

            let guard = mutex.lock();
            assert_eq!(*guard, nthreads);
        })
    }
}
