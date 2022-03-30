use std::default::Default;
use std::marker::PhantomData;

/// Convert an enum variant into an index value. For an enum with `N` variants, each enum variant
/// must map to a unique index in the range \[0,N).
pub trait IntoIndex {
    fn into_index(self) -> usize;
}

/// Convert an index into an enum variant. The result must be the inverse of
/// `IntoIndex::into_index()` (ie. for all enum variants, `x.into_index().from_index().unwrap() ==
/// x`).  Returns `None` if there is no enum variant for the provided index.
pub trait FromIndex
where
    Self: Sized,
{
    fn from_index(val: usize) -> Option<Self>;
}

/// A map from enum variants to values. Each value will be default-initialized. The enum must
/// implement `IntoIndex` and `FromIndex`, and the generic const `N` must be equal to the total
/// number of enum variants.
pub struct EnumMap<K, V, const N: usize> {
    map: [V; N],
    _phantom: PhantomData<K>,
}

impl<K: IntoIndex + FromIndex, V: Default, const N: usize> EnumMap<K, V, N> {
    pub fn new() -> Self {
        if N > 1 {
            debug_assert!(K::from_index(N - 1).is_some());
        }
        debug_assert!(K::from_index(N).is_none());

        Self {
            map: [(); N].map(|_| V::default()),
            _phantom: Default::default(),
        }
    }
}

impl<K: IntoIndex + FromIndex, V, const N: usize> EnumMap<K, V, N> {
    pub fn get(&self, key: K) -> &V {
        &self.map[key.into_index()]
    }

    pub fn get_mut(&mut self, key: K) -> &mut V {
        &mut self.map[key.into_index()]
    }

    /// Insert a value, returning the old value.
    pub fn insert(&mut self, key: K, mut val: V) -> V {
        let index = key.into_index();
        std::mem::swap(&mut self.map[index], &mut val);
        val
    }
}

impl<K: IntoIndex + FromIndex, V, const N: usize> std::ops::Index<K> for EnumMap<K, V, N> {
    type Output = V;

    fn index(&self, x: K) -> &Self::Output {
        self.get(x)
    }
}

impl<K: IntoIndex + FromIndex, V, const N: usize> std::ops::IndexMut<K> for EnumMap<K, V, N> {
    fn index_mut(&mut self, x: K) -> &mut Self::Output {
        self.get_mut(x)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    enum TestEnum {
        A,
        B,
        C,
    }

    impl IntoIndex for TestEnum {
        fn into_index(self) -> usize {
            match self {
                Self::A => 0,
                Self::B => 1,
                Self::C => 2,
            }
        }
    }

    impl FromIndex for TestEnum {
        fn from_index(val: usize) -> Option<Self> {
            match val {
                0 => Some(Self::A),
                1 => Some(Self::B),
                2 => Some(Self::C),
                _ => None,
            }
        }
    }

    #[test]
    fn test_map() {
        let mut map: EnumMap<TestEnum, bool, 3> = EnumMap::new();

        assert_eq!(map[TestEnum::A], false);
        assert_eq!(map[TestEnum::B], false);
        assert_eq!(map[TestEnum::C], false);

        map[TestEnum::A] = false;
        map[TestEnum::B] = true;
        map[TestEnum::C] = false;

        let mut map: EnumMap<TestEnum, Option<bool>, 3> = EnumMap::new();

        assert_eq!(map[TestEnum::A], None);
        assert_eq!(map[TestEnum::B], None);
        assert_eq!(map[TestEnum::C], None);

        map[TestEnum::A] = Some(false);
        assert!(map.insert(TestEnum::B, Some(true)).is_none());
    }
}
