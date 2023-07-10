//! This file contains tests intended to be run using [loom]. See the
//! [crate-level documentation](crate) for details about running these under
//! loom.
//!
//! [loom]: <https://docs.rs/loom/latest/loom/>

mod sync;

mod lazylock_tests {
    use super::sync;
    use vasi_sync::lazy_lock::LazyLock;

    #[test]
    fn test_basic() {
        sync::model(|| {
            #[cfg(not(loom))]
            let my_static = sync::Arc::new(LazyLock::const_new(|| 42));
            #[cfg(loom)]
            let my_static = sync::Arc::new(LazyLock::new(|| 42));

            assert_eq!(*my_static.force(), 42);
        })
    }

    #[test]
    fn test_multithread() {
        sync::model_with_max_preemptions(2, || {
            let my_static = sync::Arc::new(LazyLock::new(|| 42));
            // We can only create up to one fewer than loom's MAX_THREADS, which is currently 4.
            // https://docs.rs/loom/latest/loom/#combinatorial-explosion-with-many-threads
            //
            // This should be enough under loom, anyway.
            #[cfg(loom)]
            let nthreads = loom::MAX_THREADS - 1;
            #[cfg(not(loom))]
            let nthreads = 100;

            let threads: Vec<_> = (0..nthreads)
                .map(|_| {
                    let my_static = my_static.clone();
                    sync::thread::spawn(move || {
                        assert_eq!(*my_static.force(), 42);
                    })
                })
                .collect();

            for thread in threads {
                thread.join().unwrap();
            }
        })
    }

    /// Test the `deref` method, which isn't testable under loom.
    #[cfg(not(loom))]
    #[test]
    fn test_multithread_deref() {
        let my_static = sync::Arc::new(LazyLock::new(|| 42));
        let nthreads = 100;

        let threads: Vec<_> = (0..nthreads)
            .map(|_| {
                let my_static = my_static.clone();
                sync::thread::spawn(move || {
                    assert_eq!(**my_static, 42);
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }
    }

    #[cfg(not(loom))]
    #[test]
    fn test_multithread_slow_init() {
        use std::time::Duration;

        let my_static = sync::Arc::new(LazyLock::new(|| {
            std::thread::sleep(Duration::from_millis(100));
            42
        }));
        let nthreads = 100;

        let threads: Vec<_> = (0..nthreads)
            .map(|_| {
                let my_static = my_static.clone();
                sync::thread::spawn(move || {
                    assert_eq!(*my_static.force(), 42);
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }
    }
}
