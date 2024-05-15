use std::collections::HashSet;
use std::sync::RwLock;

/// A [`HashSet`] that only allows insertions and uses interior mutablity. This allows it to be used
/// in a global static.
#[derive(Debug, Default)]
pub struct OnceSet<T>(RwLock<Option<HashSet<T>>>);

impl<T> OnceSet<T>
where
    T: std::cmp::Eq + std::hash::Hash,
{
    pub const fn new() -> Self {
        Self(RwLock::new(None))
    }

    /// Insert `val` into the set. Returns `false` if `val` had previously been added to the set;
    /// otherwise returns `true`.
    pub fn insert(&self, val: T) -> bool {
        // first check with a (cheap) read-lock
        if self
            .0
            .read()
            .unwrap()
            .as_ref()
            .map(|x| x.contains(&val))
            .unwrap_or(false)
        {
            // already added
            return false;
        }

        // If it looks like we haven't already added the value, add it to the set. Also detect the
        // (rare) case that another thread already added the value after we released the read-lock
        // above.
        self.0
            .write()
            .unwrap()
            .get_or_insert_with(HashSet::new)
            .insert(val)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_once_set() {
        let set = OnceSet::new();

        assert!(set.insert("FOO".to_string()));
        assert!(set.insert("BAR".to_string()));
        assert!(!set.insert("FOO".to_string()));
        assert!(!set.insert("BAR".to_string()));
        assert!(!set.insert("BAR".to_string()));
        assert!(set.insert("XYZ".to_string()));
        assert!(!set.insert("XYZ".to_string()));
    }
}
