//! This file contains tests intended to be run using [loom]. See the
//! [crate-level documentation](crate) for details about running these under
//! loom.
//!
//! [loom]: <https://docs.rs/loom/latest/loom/>

mod sync;

mod atomic_tls_map_tests {
    use std::cell::RefCell;
    use std::num::NonZeroUsize;

    use super::sync;

    use nix::sys::wait::WaitStatus;
    use vasi_sync::atomic_tls_map::AtomicTlsMap;

    #[test]
    fn test_empty() {
        sync::model(|| {
            let _table = AtomicTlsMap::<10, u32>::new();
        })
    }

    #[test]
    fn test_store_and_load() {
        sync::model(|| {
            let table = AtomicTlsMap::<10, u32>::new();
            unsafe {
                assert_eq!(
                    *table.get_or_insert_with(NonZeroUsize::try_from(1).unwrap(), || 11),
                    11
                );
                assert_eq!(
                    *table.get_or_insert_with(NonZeroUsize::try_from(2).unwrap(), || 12),
                    12
                );

                // `get_or_insert_with` again with the same keys should return the previous
                // values.
                assert_eq!(
                    *table.get_or_insert_with(NonZeroUsize::try_from(1).unwrap(), || 1000),
                    11
                );
                assert_eq!(
                    *table.get_or_insert_with(NonZeroUsize::try_from(2).unwrap(), || 1000),
                    12
                );

                // Likewise `get` should return the inserted values.
                assert_eq!(
                    table
                        .get(NonZeroUsize::try_from(1).unwrap())
                        .as_deref()
                        .copied(),
                    Some(11)
                );
                assert_eq!(
                    table
                        .get(NonZeroUsize::try_from(2).unwrap())
                        .as_deref()
                        .copied(),
                    Some(12)
                );
            }
        })
    }

    #[test]
    fn test_store_load_remove() {
        sync::model(|| {
            let table = AtomicTlsMap::<10, u32>::new();
            unsafe {
                assert_eq!(
                    *table.get_or_insert_with(NonZeroUsize::try_from(1).unwrap(), || 11),
                    11
                );
                assert_eq!(
                    *table.get_or_insert_with(NonZeroUsize::try_from(2).unwrap(), || 12),
                    12
                );

                assert_eq!(table.remove(NonZeroUsize::try_from(1).unwrap()), Some(11));

                assert_eq!(
                    table
                        .get(NonZeroUsize::try_from(1).unwrap())
                        .as_deref()
                        .copied(),
                    None
                );
                assert_eq!(
                    table
                        .get(NonZeroUsize::try_from(2).unwrap())
                        .as_deref()
                        .copied(),
                    Some(12)
                );
            }
        })
    }

    #[test]
    fn test_forget_all_and_reuse_key() {
        sync::model(|| {
            unsafe {
                let table = sync::Arc::new(AtomicTlsMap::<10, sync::Arc<i32>>::new());
                let key = NonZeroUsize::try_from(1).unwrap();
                let value = sync::Arc::new(1);

                // AtomicTlsMap::forget_all is intended for use with fork. We
                // can't do an actual fork in loom, but we can partly simulate
                // the result of it by having the "other" thread manipulate the
                // table and then forget its references.
                let other_thread = {
                    let table = table.clone();
                    let value = value.clone();
                    sync::thread::spawn(move || {
                        let value = table.get_or_insert_with(key, || value);

                        // Under loom we have to Drop the reference to the
                        // UnsafeCell containing the value in the table for it not
                        // to consider overwriting it later an error.
                        //
                        // i.e. there's no way to tell loom that the thread
                        // holding the read-reference to the UnsafeCell will not
                        // actually read it again.
                        #[cfg(not(loom))]
                        core::mem::forget(value);
                    })
                };

                // For test purposes we need to join the other thread first to
                // guarantee it's no longer manipulating the table.
                // After a fork, this is guaranteed by the OS.
                //
                // Doing the join gives us a stronger property than we would
                // actually get post-fork, since it's guaranteed that the thread
                // completed, and therefore finished its operations on the
                // table, not just that it won't write to the table anymore.
                // TODO: Some way to more precisely model this weaker property
                // in loom?
                other_thread.join().unwrap();

                // It is now safe to forget.
                table.forget_all();

                // The value stored in the table will have "leaked".
                assert_eq!(sync::Arc::strong_count(&value), 2);

                // We can now safely reuse the key. It will overwrite the
                // raw data in the table, but won't try to Drop or manipulate it.
                assert_eq!(**table.get_or_insert_with(key, || sync::Arc::new(42)), 42);

                // The reference count won't have changed, since the value in
                // the table was not Dropped, just overwritten.
                assert_eq!(sync::Arc::strong_count(&value), 2);

                // Recover the leaked reference to avoid loom failing the test
                // due to memory leak.
                let value = sync::Arc::into_raw(value);
                sync::Arc::decrement_strong_count(value);
                sync::Arc::decrement_strong_count(value);
            };
        })
    }

    // Test `forget_all` with a real `fork`. More precisely replicates the intended
    // use-case, but can't run under loom or miri.
    #[cfg(all(not(miri), not(loom)))]
    #[test]
    fn test_forget_all_and_reuse_key_with_fork() {
        let table = AtomicTlsMap::<10, RefCell<i32>>::new();
        let key = NonZeroUsize::try_from(1).unwrap();

        let value = unsafe { table.get_or_insert_with(key, || RefCell::new(1)) };

        let fork_rv = unsafe { nix::unistd::fork() }.unwrap();
        let child = match fork_rv {
            nix::unistd::ForkResult::Parent { child } => child,
            nix::unistd::ForkResult::Child => {
                // Ensure we exit with non-zero exit code on panic.
                std::panic::set_hook(Box::new(|info| {
                    eprintln!("panic: {info:?}");
                    unsafe { libc::exit(1) };
                }));

                // Parent thread doesn't exist in the child process.
                // We can safely forget its entry.
                unsafe { table.forget_all() };

                // We can reuse the same key, which should store an entry in the same
                // address still in use by the parent. But since our memory is copy-on-write
                // copy, this will WAI.
                let value = unsafe { table.get_or_insert_with(key, || RefCell::new(42)) };
                assert_eq!(*value.borrow(), 42);

                // Exit the child process
                unsafe { libc::exit(0) };
            }
        };

        // Intentionally "race" with the child.
        assert_eq!(*value.borrow(), 1);
        *value.borrow_mut() = 2;
        assert_eq!(*value.borrow(), 2);

        // Wait for child to exit, verifying it exited normally.
        assert_eq!(
            nix::sys::wait::waitpid(child, None),
            Ok(WaitStatus::Exited(child, 0))
        );
    }

    #[test]
    fn test_drop() {
        sync::model(|| {
            let value = sync::Arc::new(());
            unsafe {
                let table = AtomicTlsMap::<10, sync::Arc<()>>::new();
                let key = NonZeroUsize::try_from(1).unwrap();
                table.get_or_insert_with(key, || value.clone());
                assert_eq!(sync::Arc::strong_count(&value), 2);
                table.remove(key);
                assert_eq!(sync::Arc::strong_count(&value), 1);
                table.get_or_insert_with(key, || value.clone());
                assert_eq!(sync::Arc::strong_count(&value), 2);
            };
            // Dropping the table should reduce the count back to 1.
            assert_eq!(sync::Arc::strong_count(&value), 1);
        })
    }

    #[test]
    fn test_cross_thread_drop() {
        sync::model(|| {
            let value = sync::Arc::new(());
            let table = sync::Arc::new(AtomicTlsMap::<10, sync::Arc<()>>::new());

            let thread = {
                let value = value.clone();
                let table = table.clone();
                sync::thread::spawn(move || {
                    unsafe {
                        let key = NonZeroUsize::try_from(1).unwrap();
                        table.get_or_insert_with(key, || value.clone());
                    };
                })
            };
            thread.join().unwrap();

            assert_eq!(sync::Arc::strong_count(&value), 2);
            drop(table);
            assert_eq!(sync::Arc::strong_count(&value), 1);
        })
    }

    #[test]
    fn test_get_and_remove_threaded() {
        sync::model_with_max_preemptions(2, || {
            // Even with the preemption bound, this test takes a long time with
            // any more than 3 threads in loom.
            #[cfg(loom)]
            const NTHREADS: usize = 3;
            #[cfg(not(loom))]
            const NTHREADS: usize = 100;

            type Table = AtomicTlsMap<NTHREADS, usize>;
            let table = sync::Arc::new(Table::new());

            let threads: Vec<_> = (0..NTHREADS)
                .map(|i| {
                    let table = table.clone();
                    let key = NonZeroUsize::try_from(i + 1).unwrap();
                    let value_offset = 10;
                    sync::thread::Builder::new()
                        .name(format!("{key:?}"))
                        .spawn(move || unsafe {
                            sync::rand_sleep();
                            assert_eq!(
                                *table.get_or_insert_with(key, || key.get() + value_offset),
                                key.get() + value_offset
                            );
                            sync::rand_sleep();
                            assert_eq!(table.remove(key), Some(key.get() + value_offset));
                            sync::rand_sleep();
                        })
                        .unwrap()
                })
                .collect();
            for thread in threads {
                thread.join().unwrap();
            }
        })
    }

    // This test seems to cause mysterious crashes in loom itself when running tests
    // with `--test-threads` > 1. Reducing max_preemptions or NTHREADS has
    // temporarily fixed it in the past, and then adding or changing *some other
    // test* brings the failure back.
    // <https://github.com/tokio-rs/loom/issues/316>
    #[test]
    fn test_reuse_keys_after_thread_exit() {
        sync::model_with_max_preemptions(2, || {
            #[cfg(loom)]
            const NTHREADS: usize = 3;
            #[cfg(not(loom))]
            const NTHREADS: usize = 100;

            type Table = AtomicTlsMap<NTHREADS, usize>;
            let table = sync::Arc::new(Table::new());

            let keys: Vec<_> = (0..NTHREADS)
                .map(|i| NonZeroUsize::try_from(i + 1).unwrap())
                .collect();

            fn thread_fn(key: NonZeroUsize, table: &Table, value_offset: usize) {
                unsafe {
                    sync::rand_sleep();
                    assert_eq!(
                        *table.get_or_insert_with(key, || key.get() + value_offset),
                        key.get() + value_offset
                    );
                    sync::rand_sleep();
                    assert_eq!(table.remove(key), Some(key.get() + value_offset));
                    sync::rand_sleep();
                }
            }

            fn thread_for_key(
                key: NonZeroUsize,
                table: sync::Arc<Table>,
            ) -> sync::thread::JoinHandle<()> {
                sync::thread::Builder::new()
                    .name(format!("{key:?}"))
                    .spawn(move || thread_fn(key, &table, 10))
                    .unwrap()
            }

            let keys_and_threads: Vec<_> = keys
                .iter()
                .map(|key| (*key, thread_for_key(*key, table.clone())))
                .collect();

            // As threads finish, reuse their keys on this thread.
            for (key, thread) in keys_and_threads {
                thread.join().unwrap();
                thread_fn(key, &table, 20);
            }
        })
    }
}
