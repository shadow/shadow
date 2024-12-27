//! Tools for chaining borrows of [`std::cell::RefCell`].
//!
//! Useful for returning an abstract borrow, where multiple inner borrows are required. e.g.:
//!
//! ```
//! use std::cell::RefCell;
//!
//! struct MyPrivateType {
//!   x: RefCell<i32>
//! }
//!
//! pub struct MyPublicType {
//!   inner: RefCell<MyPrivateType>
//! }
//!
//! impl MyPublicType {
//!   pub fn borrow_x(&self) -> impl std::ops::Deref<Target=i32> + '_ {
//!     std_util::nested_ref::NestedRef::map(self.inner.borrow(), |inner| inner.x.borrow())
//!   }
//! }
//! ```
//!
//! Currently only supports nested RefCells; i.e. one level of nesting.
//!
//! TODO: It might be feasible to genericize the reference types, which would
//! also add support for arbitrary levels of nesting.
use std::cell::{Ref, RefMut};

/// A nested [`std::cell::Ref`]. Useful for chaining borrows with [`std::cell::RefCell`].
pub struct NestedRef<'a, Inner, Outer>
where
    Inner: 'static,
{
    // unsafely points to the Ref inside `_outer`.
    // Must be declared first so that it's dropped first.
    inner: Ref<'a, Inner>,
    // Boxed so that `Self` is movable without breaking `inner`'s reference.
    _outer: Box<Ref<'a, Outer>>,
}
impl<'a, Inner, Outer> NestedRef<'a, Inner, Outer>
where
    Inner: 'static,
{
    #[inline]
    pub fn map(outer: Ref<'a, Outer>, borrow_fn: impl FnOnce(&Outer) -> Ref<Inner>) -> Self {
        Self::filter_map(outer, |outer| Some(borrow_fn(outer))).unwrap()
    }

    #[inline]
    pub fn filter_map(
        outer: Ref<'a, Outer>,
        borrow_fn: impl FnOnce(&Outer) -> Option<Ref<Inner>>,
    ) -> Option<Self> {
        let boxed_outer = Box::new(outer);
        let inner: Ref<Inner> = borrow_fn(&boxed_outer)?;
        // SAFETY: The lifetime of the `inner` returned from `borrow_fn` is the
        // (anonymous) lifetime of `boxed_outer`. However we only provided the
        // closure with the reference to the *contents* of the box, `outer`, which
        // has lifetime 'a. This is safe as long as we ensure that the transmuted
        // `boxed_outer` outlives the transmuted `inner`.
        let inner: Ref<'a, Inner> = unsafe { std::mem::transmute(inner) };
        Some(Self {
            inner,
            _outer: boxed_outer,
        })
    }
}
impl<Inner, Outer> std::ops::Deref for NestedRef<'_, Inner, Outer>
where
    Inner: 'static,
{
    type Target = Inner;

    fn deref(&self) -> &Self::Target {
        self.inner.deref()
    }
}

/// A nested [`std::cell::Ref`]. Useful for chaining a mutable borrow of a
/// [`std::cell::RefCell`].
pub struct NestedRefMut<'a, Inner, Outer>
where
    Inner: 'static,
{
    // unsafely points to the Ref inside `_outer`.
    // Must be declared first so that it's dropped first.
    inner: RefMut<'a, Inner>,
    // Boxed so that `Self` is movable without breaking `inner`'s reference.
    _outer: Box<Ref<'a, Outer>>,
}
impl<'a, Inner, Outer> NestedRefMut<'a, Inner, Outer>
where
    Inner: 'static,
{
    #[inline]
    pub fn map(outer: Ref<'a, Outer>, borrow_fn: impl FnOnce(&Outer) -> RefMut<Inner>) -> Self {
        Self::filter_map(outer, |outer| Some(borrow_fn(outer))).unwrap()
    }

    #[inline]
    pub fn filter_map(
        outer: Ref<'a, Outer>,
        borrow_fn: impl FnOnce(&Outer) -> Option<RefMut<Inner>>,
    ) -> Option<Self> {
        let outer = Box::new(outer);
        let inner: RefMut<Inner> = borrow_fn(&outer)?;
        // SAFETY: The lifetime of the `inner` returned from `borrow_fn` is the
        // (anonymous) lifetime of `boxed_outer`. However we only provided the
        // closure with the reference to the *contents* of the box, `outer`, which
        // has lifetime 'a. This is safe as long as we ensure that the transmuted
        // `boxed_outer` outlives the transmuted `inner`.
        let inner: RefMut<'a, Inner> = unsafe { std::mem::transmute(inner) };
        Some(Self {
            inner,
            _outer: outer,
        })
    }
}
impl<Inner, Outer> std::ops::Deref for NestedRefMut<'_, Inner, Outer>
where
    Inner: 'static,
{
    type Target = Inner;

    fn deref(&self) -> &Self::Target {
        self.inner.deref()
    }
}
impl<Inner, Outer> std::ops::DerefMut for NestedRefMut<'_, Inner, Outer>
where
    Inner: 'static,
{
    fn deref_mut(&mut self) -> &mut Self::Target {
        self.inner.deref_mut()
    }
}

#[cfg(test)]
mod tests {
    use std::cell::RefCell;

    use super::*;

    struct TestOuter {
        x: RefCell<i32>,
    }

    #[test]
    fn nestedref_map() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        let nested = NestedRef::map(outer.borrow(), |inner| inner.x.borrow());
        assert_eq!(*nested, 42);
    }

    #[test]
    fn nestedref_filter_map_some() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        let nested = NestedRef::filter_map(outer.borrow(), |inner| Some(inner.x.borrow()));
        let nested = nested.unwrap();
        assert_eq!(*nested, 42);
    }

    #[test]
    fn nestedref_filter_map_none() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        let nested = NestedRef::<i32, TestOuter>::filter_map(outer.borrow(), |_inner| None);
        assert!(nested.is_none());
    }

    #[test]
    fn nestedref_is_movable() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        let nested = NestedRef::map(outer.borrow(), |inner| inner.x.borrow());
        assert_eq!(*nested, 42);
        let boxed_nested = Box::new(nested);
        assert_eq!(**boxed_nested, 42);
    }

    #[test]
    fn nestedrefmut_map() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        {
            let mut nested = NestedRefMut::map(outer.borrow(), |inner| inner.x.borrow_mut());
            assert_eq!(*nested, 42);
            *nested += 1;
            assert_eq!(*nested, 43);
            assert!(outer.borrow().x.try_borrow().is_err());
        }
        assert_eq!(*outer.borrow().x.borrow(), 43);
    }

    #[test]
    fn nestedrefmut_filter_map_some() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        {
            let nested =
                NestedRefMut::filter_map(outer.borrow(), |inner| Some(inner.x.borrow_mut()));
            let mut nested = nested.unwrap();
            assert_eq!(*nested, 42);
            *nested += 1;
            assert_eq!(*nested, 43);
            assert!(outer.borrow().x.try_borrow().is_err());
        }
        assert_eq!(*outer.borrow().x.borrow(), 43);
    }

    #[test]
    fn nestedrefmut_filter_map_none() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        let nested = NestedRefMut::<i32, TestOuter>::filter_map(outer.borrow(), |_inner| None);
        assert!(nested.is_none());
    }

    #[test]
    fn nestedrefmut_is_movable() {
        let outer = RefCell::new(TestOuter {
            x: RefCell::new(42),
        });
        {
            let nested = NestedRefMut::map(outer.borrow(), |inner| inner.x.borrow_mut());
            assert_eq!(*nested, 42);
            let mut nested = Box::new(nested);
            assert_eq!(**nested, 42);
            **nested += 1;
            assert_eq!(**nested, 43);
        }
        assert_eq!(*outer.borrow().x.borrow(), 43);
    }
}
