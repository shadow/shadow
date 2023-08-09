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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_new_small_slice() {
        SmallArrayBackedSlice::<3, u8>::empty();
        SmallArrayBackedSlice::<3, u8>::new(&[]).unwrap();
        SmallArrayBackedSlice::<3, u8>::new(&[1]).unwrap();
        SmallArrayBackedSlice::<3, u8>::new(&[1, 2]).unwrap();
        SmallArrayBackedSlice::<3, u8>::new(&[1, 2, 3]).unwrap();
        assert!(SmallArrayBackedSlice::<3, u8>::new(&[1, 2, 3, 4]).is_none());
    }

    #[test]
    fn test_deref_small_slice() {
        let slice = SmallArrayBackedSlice::<3, u8>::empty();
        assert!(slice.is_empty());

        let slice = SmallArrayBackedSlice::<3, u8>::new(&[]).unwrap();
        assert!(slice.is_empty());

        let slice = SmallArrayBackedSlice::<3, u8>::new(&[1]).unwrap();
        assert_eq!(slice.len(), 1);

        let slice = SmallArrayBackedSlice::<3, u8>::new(&[1, 2, 3]).unwrap();
        assert_eq!(slice.len(), 3);
        assert_eq!(&*slice, &[1, 2, 3]);
    }

    #[test]
    fn test_remove_from_list() {
        let mut list: std::collections::LinkedList<u8> =
            [1, 6, 2, 7, 3, 6, 4, 0].into_iter().collect();

        fn to_vec<T: Clone>(list: &std::collections::LinkedList<T>) -> Vec<T> {
            list.clone().into_iter().collect()
        }

        remove_from_list(&mut list, &3);
        assert_eq!(&to_vec(&list), &[1, 6, 2, 7, 6, 4, 0]);

        remove_from_list(&mut list, &6);
        assert_eq!(&to_vec(&list), &[1, 2, 7, 6, 4, 0]);

        remove_from_list(&mut list, &6);
        assert_eq!(&to_vec(&list), &[1, 2, 7, 4, 0]);

        remove_from_list(&mut list, &1);
        assert_eq!(&to_vec(&list), &[2, 7, 4, 0]);

        remove_from_list(&mut list, &0);
        assert_eq!(&to_vec(&list), &[2, 7, 4]);

        remove_from_list(&mut list, &7);
        assert_eq!(&to_vec(&list), &[2, 4]);

        remove_from_list(&mut list, &4);
        assert_eq!(&to_vec(&list), &[2]);

        remove_from_list(&mut list, &3);
        assert_eq!(&to_vec(&list), &[2]);

        remove_from_list(&mut list, &2);
        assert_eq!(&to_vec(&list), &[]);

        remove_from_list(&mut list, &2);
        assert_eq!(&to_vec(&list), &[]);
    }
}
