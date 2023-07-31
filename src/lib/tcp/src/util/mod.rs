pub mod time;

/// An array that derefs to a variable-length slice. Useful for storing variable-length data in a
/// struct without allocating. The generic `N` must be a valid `u8`, so can only store up to
/// `u8::MAX` items.
#[derive(Copy, Clone, Debug)]
pub struct SmallArrayBackedSlice<const N: usize, T> {
    len: u8,
    bytes: [T; N],
}

// we could avoid the `Default` and `Copy` requirements by using `MaybeUninit`, but it's not worth
// the `unsafe` when we only plan to use this for integer arrays
impl<const N: usize, T: Default + Copy> SmallArrayBackedSlice<N, T> {
    // N isn't available from a constant context in `new()` for some reason so we need to do this
    // check here, but it doesn't actually run unless we access `CHECK_N` from somewhere like
    // `new()`
    const CHECK_N: () = {
        assert!(N <= u8::MAX as usize);
    };

    /// Returns `None` if there's not enough space.
    pub fn new(bytes: &[T]) -> Option<Self> {
        // force a compile-time check that `N` is a valid `u8`
        #[allow(clippy::let_unit_value)]
        let _ = Self::CHECK_N;

        if bytes.len() > N {
            return None;
        }

        let mut rv = Self::empty();

        rv.len = bytes.len().try_into().unwrap();
        rv.bytes[..bytes.len()].copy_from_slice(bytes);

        Some(rv)
    }

    pub fn empty() -> Self {
        // force a compile-time check that `N` is a valid `u8`
        #[allow(clippy::let_unit_value)]
        let _ = Self::CHECK_N;

        Self {
            len: 0,
            bytes: [T::default(); N],
        }
    }
}

impl<const N: usize, T> std::ops::Deref for SmallArrayBackedSlice<N, T> {
    type Target = [T];

    fn deref(&self) -> &Self::Target {
        &self.bytes[..(self.len as usize)]
    }
}

impl<const N: usize, T> AsRef<[T]> for SmallArrayBackedSlice<N, T> {
    fn as_ref(&self) -> &[T] {
        self
    }
}

/// Remove at most one item from a [`LinkedList`](std::collections::LinkedList).
pub(crate) fn remove_from_list<T: Eq>(list: &mut std::collections::LinkedList<T>, item: &T) {
    if let Some(pos) = list.iter().position(|e| e == item) {
        let mut split_list = list.split_off(pos);
        split_list.pop_front();
        list.append(&mut split_list);
    }
}
