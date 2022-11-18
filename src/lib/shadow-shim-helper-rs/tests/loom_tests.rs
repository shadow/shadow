#![cfg(loom)]

use shadow_shim_helper_rs::scmutex::{SelfContainedMutex, SelfContainedMutexGuard};

use std::pin::Pin;
use rkyv::string::ArchivedString;
use loom::sync::Arc;

#[test]
fn test_basic() {
    loom::model(|| {
        let mutex = SelfContainedMutex::new(0);
        let mut guard = mutex.lock();
        *guard += 1;
    })
}

#[test]
fn test_threads() {
    loom::model(|| {
        let numthreads = 2;
        let mutex = loom::sync::Arc::new(SelfContainedMutex::new(0));
        let threads: Vec<_> = (0..numthreads)
            .map(|_| {
                let mutex = mutex.clone();
                loom::thread::spawn(move || {
                    let mut guard = mutex.lock();
                    *guard += 1;
                })
            })
            .collect();

        for thread in threads {
            thread.join().unwrap();
        }

        let guard = mutex.lock();
        assert_eq!(*guard, numthreads);
    })
}

/*
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
fn rkyv_test_basic() {
    loom::model(|| {
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
    })
}

#[test]
fn rkyv_test_basic_compound() {
    loom::model(|| {
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
    })
}

#[test]
fn rkyv_test_inner_not_unpin() {
    loom::model(|| {
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
        let archived = unsafe { rkyv::archived_root_mut::<T>(std::pin::Pin::new(&mut bytes[..])) };
        let lock = archived.lock();
        assert_eq!(*lock, "TEST");
    })
}

#[test]
fn rkyv_test_threads() {
    loom::model(|| {
        type T = SelfContainedMutex<i32>;
        let original_mutex: T = SelfContainedMutex::new(0);
        let bytes = Arc::new(rkyv::to_bytes::<_, 256>(&original_mutex).unwrap());

        let threads: Vec<_> = (0..100)
            .map(|_| {
                let bytes = bytes.clone();
                loom::thread::spawn(move || {
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
*/