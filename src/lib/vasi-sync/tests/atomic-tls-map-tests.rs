//! This file contains tests intended to be run using [loom]. See the
//! [crate-level documentation](crate) for details about running these under
//! loom.
//!
//! [loom]: <https://docs.rs/loom/latest/loom/>

mod sync;

mod atomic_tls_map_tests {
    use std::num::NonZeroUsize;

    use super::sync;

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
                        .get(NonZeroUsize::try_from(NonZeroUsize::try_from(1).unwrap()).unwrap())
                        .as_deref()
                        .copied(),
                    None
                );
                assert_eq!(
                    table
                        .get(NonZeroUsize::try_from(NonZeroUsize::try_from(2).unwrap()).unwrap())
                        .as_deref()
                        .copied(),
                    Some(12)
                );
            }
        })
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
