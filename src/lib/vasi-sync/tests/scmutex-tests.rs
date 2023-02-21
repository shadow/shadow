//! This file contains tests intended to be run using [loom]. See the
//! [crate-level documentation](crate) for details about running these under
//! loom.
//!
//! [loom]: <https://docs.rs/loom/latest/loom/>
use vasi_sync::scmutex::{SelfContainedMutex, SelfContainedMutexGuard};

mod sync;

mod tests {
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
        sync::model(|| {
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

// Crashes under Loom. I suspect it may have to do with writing the internal
// atomic through a pointer. e.g. an AtomicU32 isn't represented internally as
// just a u32, and aren't repr(transparent) or even repr(C).
// TODO: Is there a way to test this code under loom?
#[cfg(not(loom))]
mod rkyv_tests {
    use std::pin::Pin;

    use rkyv::string::ArchivedString;

    use super::*;

    /// Taking the AlignedVec here instead of a slice avoids a gray safety area
    /// that violates miri's current Stacked Borrows check.
    /// https://github.com/rkyv/rkyv/issues/303
    ///
    /// SAFETY:
    /// * `vec` must contain a value of type `T` at the end.
    unsafe fn archived_root<T>(vec: &rkyv::AlignedVec) -> &T::Archived
    where
        T: rkyv::Archive,
    {
        let pos = vec.len() - std::mem::size_of::<T::Archived>();
        // SAFETY:
        // * Caller guarantees that this memory contains a valid `T`
        // * The reference borrows from `vec`, ensuring that it can't
        //   be dropped or mutated.
        // * `AlignedVec::as_ptr` explicitly supports this use-case,
        //   including for `T` that has internal mutability via `UnsafeCell`.
        unsafe { &*vec.as_ptr().add(pos).cast() }
    }

    #[test]
    fn test_basic() {
        sync::model(|| {
            type T = SelfContainedMutex<i32>;
            let original_mutex: T = SelfContainedMutex::new(10);
            let bytes = rkyv::to_bytes::<_, 256>(&original_mutex).unwrap();

            // The archived mutex can be used to mutate the data in place.
            {
                let archived = unsafe { archived_root::<T>(&bytes) };
                let mut lock = archived.lock();
                assert_eq!(*lock, 10);
                *lock += 1;
            }

            // Re-constituting the archive should still give the new value.
            let archived = unsafe { archived_root::<T>(&bytes) };
            let lock = archived.lock();
            assert_eq!(*lock, 11);
        });
    }

    #[test]
    fn test_basic_compound() {
        sync::model(|| {
            type T = [SelfContainedMutex<(i32, i32)>; 2];
            let original_mutexes: T = [
                SelfContainedMutex::new((1, 2)),
                SelfContainedMutex::new((3, 4)),
            ];
            let bytes = rkyv::to_bytes::<_, 256>(&original_mutexes).unwrap();

            let archived = unsafe { archived_root::<T>(&bytes) };
            assert_eq!(archived[0].lock().0, 1);
            assert_eq!(archived[0].lock().1, 2);
            assert_eq!(archived[1].lock().0, 3);
            assert_eq!(archived[1].lock().1, 4);
        });
    }

    #[test]
    fn test_inner_not_unpin() {
        sync::model(|| {
            type T = SelfContainedMutex<String>;
            let original_mutex: T = SelfContainedMutex::new(String::from("test"));
            let mut bytes = rkyv::to_bytes::<_, 256>(&original_mutex).unwrap();

            {
                let archived: &SelfContainedMutex<ArchivedString> =
                    unsafe { archived_root::<T>(&bytes) };
                // Because `ArchivedString` is `!Unpin`, we need to pin the mutex for it to allow
                // mutable access.
                //
                // SAFETY: We never move the underlying data (e.g. by mutating `bytes`)
                let archived = unsafe { Pin::new_unchecked(archived) };
                let lock = archived.lock_pinned();
                assert_eq!(*lock, "test");

                // Because `ArchivedString` is `!Unpin`, we can't directly access the mutable
                // reference to it. We need to use `map_pinned` instead.
                SelfContainedMutexGuard::map_pinned(lock, |strref| {
                    let mut strref = strref.pin_mut_str();
                    strref.make_ascii_uppercase();
                    assert_eq!(&*strref, "TEST");
                });
            }

            // Re-constituting the archive should still give the new value.
            let archived =
                unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes[..])) };
            let lock = archived.lock();
            assert_eq!(*lock, "TEST");
        })
    }

    #[test]
    fn test_threads() {
        sync::model(|| {
            type T = SelfContainedMutex<i32>;
            let original_mutex: T = SelfContainedMutex::new(0);
            let bytes = sync::Arc::new(rkyv::to_bytes::<_, 256>(&original_mutex).unwrap());

            let threads: Vec<_> = (0..100)
                .map(|_| {
                    let bytes = bytes.clone();
                    sync::thread::spawn(move || {
                        // No need to pin here, since i32 implements Unpin.
                        let archived = unsafe { archived_root::<T>(&bytes) };
                        let mut guard = archived.lock();
                        *guard += 1;
                    })
                })
                .collect();

            for thread in threads {
                thread.join().unwrap();
            }

            let archived = unsafe { archived_root::<T>(&bytes) };
            let guard = archived.lock();
            assert_eq!(*guard, 100);
        })
    }
}
