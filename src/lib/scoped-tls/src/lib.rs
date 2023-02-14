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