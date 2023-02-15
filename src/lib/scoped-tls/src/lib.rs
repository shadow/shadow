use std::cell::{RefCell, Ref};
use std::ffi::c_void;
use std::marker::PhantomData;
use std::ops::Deref;

pub struct ScopedTls<Location, Value> {
    // XXX This doesn't *really* need to behave as a T, but in intended usage T
    // shouldn't introduce any constraints. It's just a unique marker type.
    _phantom_marker: PhantomData<Location>,
    // The actual values are stored in thread-local storage.
    _phantom_value: PhantomData<Value>,
}

impl<Location, Value> ScopedTls<Location, Value> where Value: 'static {
    // We can't use a generic type in a static variable definition.
    // https://users.rust-lang.org/t/cant-use-generic-parameters-from-outer-function/62390/3
    //
    // Just using a void pointer for now, but there's probably a better way.
    // e.g. a macro could put in the concrete type.
    thread_local!(static CURRENT: RefCell<*const c_void>  = RefCell::new(std::ptr::null()));

    /// Panics if called recursively, or if `f` returns while there are still references
    /// to `val` via `Self::current`.
    pub fn with_current_set_to(val: &Value, f: impl FnOnce()) {
        Self::CURRENT.with(|current| {
            // This will panic if there are live borrows.
            let prev = current.replace(val as * const _ as *const c_void);
            // Also panic if there was already a value set, even if it wasn't borrowed.
            // XXX Maybe not strictly necessary.
            assert!(prev.is_null());
            f();
            // Panics if there are live borrows.
            current.replace(prev);
        })
    }

    pub fn current() -> Option<impl Deref<Target = Value>> {
        Self::CURRENT.with(|current| {
            let current: &RefCell<*const c_void> = current;
            // SAFETY: While the `RefCell` could get destroyed in the case of a `panic`,
            // the `Ref` we're returning is already `!UnwindSafe`.
            // XXX: Are there other dangers here I'm missing?
            let current: &'static RefCell<*const c_void> = unsafe { std::mem::transmute(current)};
            Ref::filter_map(current.borrow(), |val| {
                let val = *val as *const Value;
                // SAFETY: `with_current_set_to` ensures this pointer is
                // dereferenceable, and that the reference will not be
                // invalidated while this `Ref` still lives.
                unsafe { val.as_ref() }
            }).ok()
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn get_current() {
        struct MyMarker(());
        type MyScopedTls = ScopedTls<MyMarker, i32>;
        assert!(MyScopedTls::current().is_none());
        MyScopedTls::with_current_set_to(&42, || {
            assert_eq!(*MyScopedTls::current().unwrap(), 42);
        });
        assert!(MyScopedTls::current().is_none());
    }

    #[test]
    #[should_panic]
    fn recursion_panics() {
        struct MyMarker(());
        type MyScopedTls = ScopedTls<MyMarker, i32>;
        MyScopedTls::with_current_set_to(&42, || {
            MyScopedTls::with_current_set_to(&43, || ());
        });
    }

    #[test]
    #[should_panic]
    fn leak_panics() {
        struct MyMarker(());
        type MyScopedTls = ScopedTls<MyMarker, i32>;
        MyScopedTls::with_current_set_to(&42, || {
            Box::leak(Box::new(MyScopedTls::current()));
        });
    }
}

use std::cell::Cell;

pub struct ScopedTlsPthread<T> {
    pthread_key: Cell<Option<libc::pthread_key_t>>,
    // The actual values are stored in thread-local storage.  We don't actually
    // need to inherit properties of `T` here, but can't easily avoid it,
    // other than e.g. to explicitly mark Self as `Sync` even if `T` isn't.
    _phantom_value: PhantomData<T>,
}

// SAFETY: TODO
unsafe impl<T> Sync for ScopedTlsPthread<T> {}

impl<T> ScopedTlsPthread<T>
where
    T: 'static,
{
    extern "C" fn destructor(val: *mut libc::c_void) {
        let val = val as *mut RefCell<*const T>;
        // pthread_key_create doesn't call the destructor for NULL keys.
        debug_assert!(!val.is_null());
        drop(unsafe { Box::from_raw(val) })
    }

    #[inline]
    fn key(&self) -> libc::pthread_key_t {
        let key = self.pthread_key.get();
        key.unwrap_or_else(|| {
            let mut key: libc::pthread_key_t = 0;
            let res = unsafe { libc::pthread_key_create(&mut key, Some(Self::destructor)) };
            // XXX: better error handling
            assert_eq!(res, 0);
            self.pthread_key.set(Some(key));
            key
        })
    }

    #[inline]
    fn thread_refcell(&self) -> &'static RefCell<*const T> {
        let key = self.key();
        let ptr = unsafe { libc::pthread_getspecific(key) } as *const RefCell<*const T>;
        let ptrref = unsafe { ptr.as_ref() };
        ptrref.unwrap_or_else(|| {
            let val = Box::into_raw(Box::new(RefCell::new(std::ptr::null())));
            unsafe { libc::pthread_setspecific(key, val as *const _ as *const c_void) };
            unsafe { &*val }
        })
    }

    pub const fn new() -> Self {
        Self {
            pthread_key: Cell::new(None),
            _phantom_value: PhantomData,
        }
    }

    /// Panics if called recursively, or if `f` returns while there are still references
    /// to `val` via `Self::current`.
    pub fn with_current_set_to<F, R>(&self, val: &T, f: F)
    where
        F: FnOnce() -> R,
    {
        let current = self.thread_refcell();
        // This will panic if there are live borrows.
        let prev = current.replace(val as *const _);
        // Also panic if there was already a value set, even if it wasn't borrowed.
        // XXX Maybe not strictly necessary.
        assert!(prev.is_null());
        f();
        // Panics if there are live borrows.
        current.replace(prev);
    }

    #[inline]
    pub fn current(&self) -> Option<impl Deref<Target = T>> {
        let current = self.thread_refcell();
        // SAFETY: While the `RefCell` could get destroyed in the case of a `panic`,
        // the `Ref` we're returning is already `!UnwindSafe`.
        // XXX: Are there other dangers here I'm missing?
        let current: &'static RefCell<*const T> = unsafe { std::mem::transmute(current) };
        Ref::filter_map(current.borrow(), |val| {
            let val = *val as *const T;
            // SAFETY: `with_current_set_to` ensures this pointer is
            // dereferenceable, and that the reference will not be
            // invalidated while this `Ref` still lives.
            unsafe { val.as_ref() }
        })
        .ok()
    }
}

#[cfg(test)]
mod test2 {
    use super::*;

    #[test]
    fn get_current() {
        static TLS: ScopedTlsPthread<i32> = ScopedTlsPthread::new();
        assert!(TLS.current().is_none());
        TLS.with_current_set_to(&42, || {
            assert_eq!(*TLS.current().unwrap(), 42);
        });
        assert!(TLS.current().is_none());
    }

    #[test]
    fn get_current_multithreaded() {
        static TLS: ScopedTlsPthread<i32> = ScopedTlsPthread::new();
        assert!(TLS.current().is_none());
        TLS.with_current_set_to(&42, || {
            assert_eq!(*TLS.current().unwrap(), 42);
            let thread = std::thread::spawn(|| {
                assert!(TLS.current().is_none());
                TLS.with_current_set_to(&4242, || {
                    assert_eq!(*TLS.current().unwrap(), 4242);
                });
                assert!(TLS.current().is_none());
            });
            assert_eq!(*TLS.current().unwrap(), 42);
            thread.join().unwrap();
        });
        assert!(TLS.current().is_none());
    }

    #[test]
    #[should_panic]
    fn recursion_panics() {
        static TLS: ScopedTlsPthread<i32> = ScopedTlsPthread::new();
        TLS.with_current_set_to(&42, || {
            TLS.with_current_set_to(&43, || ());
        });
    }

    #[test]
    #[should_panic]
    fn leak_panics() {
        static TLS: ScopedTlsPthread<i32> = ScopedTlsPthread::new();
        TLS.with_current_set_to(&42, || {
            Box::leak(Box::new(TLS.current()));
        });
    }
}
