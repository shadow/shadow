/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

/*!
A counter that can be used to count frequencies of a set of objects. The counter starts
with no keys. When an unknown key is incremented, the counter adds a new key to an
internal map and sets the count for that key to 1. When a known key is incremented, the
count for that key increases. The state of the counter can be extracted by converting it
to a string, which lists the counts for all keys sorted with the heaviest hitters first.
Currently, only String types are supported, but we may eventually support counting
generic types.
*/

use std::collections::HashMap;
use std::fmt::{Display, Formatter};
use std::ops::{Add, Sub};

use serde::ser::SerializeMap;

/// The main counter object that maps individual keys to count values.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Counter {
    // TODO: convert this so we could count generic types instead of Strings
    items: HashMap<String, i64>,
}

/// The supported operations on the values stored in this counter.
enum CounterOperation {
    Add,
    Set,
    Sub,
}

/// A collection of counters that can store and modify values for a set of keys.
impl Counter {
    /// Initializes a new counter map that starts with no keys.
    pub fn new() -> Counter {
        Counter {
            items: HashMap::new(),
        }
    }

    /// Increment the counter value by one for the key given by id.
    /// Returns the value of the counter after it was incremented.
    pub fn add_one(&mut self, id: &str) -> i64 {
        self.operate(id, CounterOperation::Add, 1)
    }

    /// Decrement the counter value by one for the key given by id.
    /// Returns the value of the counter after it was decremented.
    pub fn sub_one(&mut self, id: &str) -> i64 {
        self.operate(id, CounterOperation::Sub, 1)
    }

    /// Increment the counter value by the given value for the key given by id.
    /// Returns the value of the counter after it was incremented.
    pub fn add_value(&mut self, id: &str, value: i64) -> i64 {
        self.operate(id, CounterOperation::Add, value)
    }

    /// Decrement the counter value by the given value for the key given by id.
    /// Returns the value of the counter after it was decremented.
    pub fn sub_value(&mut self, id: &str, value: i64) -> i64 {
        self.operate(id, CounterOperation::Sub, value)
    }

    /// Sets the counter value to the given value for the key given by id.
    /// Returns the value of the counter after it was set.
    pub fn set_value(&mut self, id: &str, value: i64) -> i64 {
        self.operate(id, CounterOperation::Set, value)
    }

    /// Returns the counter value for the key given by id, or 0 if no operations have
    /// been performed on the key.
    pub fn get_value(&self, id: &str) -> i64 {
        match self.items.get(id) {
            Some(val) => *val,
            None => 0,
        }
    }

    /// Add all values for all keys in `other` to this counter.
    pub fn add_counter(&mut self, other: &Counter) {
        for (key, val) in other.items.iter() {
            self.add_value(key, *val);
        }
    }

    /// Subtract all values for all keys in `other` from this counter.
    pub fn sub_counter(&mut self, other: &Counter) {
        for (key, val) in other.items.iter() {
            self.sub_value(key, *val);
        }
    }

    /// Perform a supported operation on the counter value.
    fn operate(&mut self, id: &str, op: CounterOperation, value: i64) -> i64 {
        match self.items.get_mut(id) {
            Some(val) => {
                // Update and return the existing value without allocating new key.
                match op {
                    CounterOperation::Add => *val += value,
                    CounterOperation::Sub => *val -= value,
                    CounterOperation::Set => *val = value,
                }
                // Remove the key if the value reached 0.
                if *val == 0 {
                    assert_eq!(self.items.remove(id), Some(0));
                    0
                } else {
                    *val
                }
            }
            None => {
                // Allocate new key and insert it with initial value of 0.
                assert_eq!(self.items.insert(id.to_string(), 0), None);
                // Now that we inserted it, we can do the operation.
                self.operate(id, op, value)
            }
        }
    }

    /// Get an iterator that returns elements in the order best suited for human-readable output
    /// (currently sorted by value with the largest value first).
    fn sorted_for_display(
        &self,
    ) -> impl IntoIterator<
        IntoIter = impl ExactSizeIterator<Item = (&String, &i64)>,
        Item = (&String, &i64),
    > {
        // Get the items in a vector so we can sort them.
        let mut item_vec = Vec::from_iter(&self.items);

        // Sort the counts so our string is consistent.
        // Use reverse on vals to get the heaviest hitters first, but sort keys normally.
        item_vec.sort_by(|&(key_a, val_a), &(key_b, val_b)| {
            val_a.cmp(val_b).reverse().then(key_a.cmp(key_b))
        });

        item_vec
    }
}

impl Default for Counter {
    fn default() -> Self {
        Self::new()
    }
}

impl Display for Counter {
    /// Returns a string representation of the counter in the form
    ///   `{key1:value1, key2:value2, ..., keyN:valueN}`
    /// for known keys and values.
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        let items = self.sorted_for_display().into_iter();
        let items_len = items.len();

        // Create a string representation of the counts by iterating over the items.
        write!(f, "{{")?;
        for (i, item) in items.enumerate() {
            write!(f, "{}:{}", item.0, item.1)?;
            if i < items_len - 1 {
                write!(f, ", ")?;
            }
        }
        write!(f, "}}")
    }
}

impl serde::Serialize for Counter {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        let items = self.sorted_for_display().into_iter();
        let mut map = serializer.serialize_map(Some(items.len()))?;
        for (k, v) in items {
            map.serialize_entry(k, v)?;
        }
        map.end()
    }
}

impl Add for Counter {
    type Output = Self;
    /// Combines two counters by adding all values for all keys of `other` to `self`.
    fn add(mut self, other: Self) -> Self {
        self.add_counter(&other);
        self
    }
}

impl Sub for Counter {
    type Output = Self;
    /// Combines two counters by subtracting all values for all keys of `other` from `self`.
    fn sub(mut self, other: Self) -> Self {
        self.sub_counter(&other);
        self
    }
}

mod export {
    use std::ffi::CStr;
    use std::ffi::CString;
    use std::os::raw::c_char;

    use super::*;

    #[no_mangle]
    pub extern "C-unwind" fn counter_new() -> *mut Counter {
        Box::into_raw(Box::new(Counter::new()))
    }

    #[no_mangle]
    pub extern "C-unwind" fn counter_free(counter_ptr: *mut Counter) {
        if counter_ptr.is_null() {
            return;
        }
        drop(unsafe { Box::from_raw(counter_ptr) });
    }

    #[no_mangle]
    pub extern "C-unwind" fn counter_add_value(
        counter: *mut Counter,
        id: *const c_char,
        value: i64,
    ) -> i64 {
        assert!(!counter.is_null());
        assert!(!id.is_null());

        let counter = unsafe { &mut *counter };
        let id = unsafe { CStr::from_ptr(id) };

        counter.add_value(id.to_str().unwrap(), value)
    }

    #[no_mangle]
    pub extern "C-unwind" fn counter_sub_value(
        counter: *mut Counter,
        id: *const c_char,
        value: i64,
    ) -> i64 {
        assert!(!counter.is_null());
        assert!(!id.is_null());

        let counter = unsafe { &mut *counter };
        let id = unsafe { CStr::from_ptr(id) };

        counter.sub_value(id.to_str().unwrap(), value)
    }

    #[no_mangle]
    pub extern "C-unwind" fn counter_add_counter(counter: *mut Counter, other: *const Counter) {
        assert!(!counter.is_null());
        assert!(!other.is_null());

        let counter = unsafe { &mut *counter };
        let other = unsafe { &*other };

        counter.add_counter(other)
    }

    #[no_mangle]
    pub extern "C-unwind" fn counter_sub_counter(counter: *mut Counter, other: *const Counter) {
        assert!(!counter.is_null());
        assert!(!other.is_null());

        let counter = unsafe { &mut *counter };
        let other = unsafe { &*other };

        counter.sub_counter(other)
    }

    #[no_mangle]
    pub extern "C-unwind" fn counter_equals_counter(
        counter: *const Counter,
        other: *const Counter,
    ) -> bool {
        assert!(!counter.is_null());
        assert!(!other.is_null());

        let counter = unsafe { &*counter };
        let other = unsafe { &*other };

        counter == other
    }

    /// Creates a new string representation of the counter, e.g., for logging.
    /// The returned string must be free'd by passing it to counter_free_string.
    #[no_mangle]
    pub extern "C-unwind" fn counter_alloc_string(counter: *mut Counter) -> *mut c_char {
        assert!(!counter.is_null());

        let counter = unsafe { &mut *counter };
        let string = counter.to_string();

        // Transfer ownership back to caller
        CString::new(string).unwrap().into_raw()
    }

    /// Frees a string previously returned from counter_alloc_string.
    #[no_mangle]
    pub extern "C-unwind" fn counter_free_string(counter: *mut Counter, ptr: *mut c_char) {
        assert!(!counter.is_null());
        assert!(!ptr.is_null());
        // Free the previously alloc'd string
        drop(unsafe { CString::from_raw(ptr) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_set_value() {
        let mut counter = Counter::new();
        assert_eq!(counter.set_value("read", 100), 100);
        assert_eq!(counter.set_value("read", 10), 10);
        assert_eq!(counter.set_value("read", 0), 0);
        assert_eq!(counter.set_value("read", 10), 10);
    }

    #[test]
    fn test_get_value() {
        let mut counter = Counter::new();
        assert_eq!(counter.get_value("read"), 0);
        assert_eq!(counter.get_value("write"), 0);
        assert_eq!(counter.get_value("close"), 0);
        counter.add_one("write");
        counter.add_one("write");
        counter.add_one("read");
        counter.add_one("write");
        assert_eq!(counter.get_value("read"), 1);
        assert_eq!(counter.get_value("write"), 3);
        assert_eq!(counter.get_value("close"), 0);
    }

    #[test]
    fn test_add_one() {
        let mut counter = Counter::new();
        assert_eq!(counter.add_one("read"), 1);
        assert_eq!(counter.add_one("read"), 2);
        assert_eq!(counter.add_one("write"), 1);
        assert_eq!(counter.add_one("read"), 3);
    }

    #[test]
    fn test_sub_one() {
        let mut counter = Counter::new();
        counter.set_value("read", 2);
        assert_eq!(counter.sub_one("read"), 1);
        assert_eq!(counter.sub_one("read"), 0);
        assert_eq!(counter.sub_one("read"), -1);
        assert_eq!(counter.get_value("read"), -1);
        counter.set_value("read", 100);
        counter.set_value("write", 100);
        assert_eq!(counter.sub_one("read"), 99);
    }

    #[test]
    fn test_add_value() {
        let mut counter = Counter::new();
        assert_eq!(counter.add_value("read", 10), 10);
        assert_eq!(counter.add_value("read", 10), 20);
        assert_eq!(counter.add_value("write", 10), 10);
        assert_eq!(counter.add_value("read", 10), 30);
    }

    #[test]
    fn test_sub_value() {
        let mut counter = Counter::new();
        counter.set_value("read", 100);
        assert_eq!(counter.sub_value("read", 10), 90);
        assert_eq!(counter.sub_value("read", 10), 80);
        assert_eq!(counter.sub_value("write", 10), -10);
        assert_eq!(counter.sub_value("read", 10), 70);
    }

    #[test]
    fn test_operator_add() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 100);

        let mut counter_b = Counter::new();
        counter_b.set_value("read", 50);

        let mut counter_sum = Counter::new();
        counter_sum.set_value("read", 150);

        // This transfers ownership of a and b to counter_added
        let counter_added = counter_a + counter_b;
        assert_eq!(counter_added.get_value("read"), 150);
        assert_eq!(counter_added, counter_sum);
    }

    #[test]
    fn test_operator_sub() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 100);

        let mut counter_b = Counter::new();
        counter_b.set_value("read", 25);

        let mut counter_sum = Counter::new();
        counter_sum.set_value("read", 75);

        // This transfers ownership of a and b to counter_subbed
        let counter_subbed = counter_a - counter_b;
        assert_eq!(counter_subbed.get_value("read"), 75);
        assert_eq!(counter_subbed, counter_sum);
    }

    #[test]
    fn test_operator_sub_multi() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 100);

        let mut counter_b = Counter::new();
        counter_b.set_value("read", 25);

        let mut counter_c = Counter::new();
        counter_c.set_value("read", 75);

        // Subtract to get negative first, then add back to zero.
        assert_eq!(counter_b - counter_a + counter_c, Counter::new());
    }

    #[test]
    fn test_add_counter() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 100);
        counter_a.set_value("write", 1);

        let mut counter_b = Counter::new();
        counter_b.set_value("read", 50);
        counter_b.set_value("write", 2);

        let mut counter_sum = Counter::new();
        counter_sum.set_value("read", 150);
        counter_sum.set_value("write", 3);

        counter_a.add_counter(&counter_b);

        assert_eq!(counter_a.get_value("read"), 150);
        assert_eq!(counter_a.get_value("write"), 3);
        assert_eq!(counter_b.get_value("read"), 50);
        assert_eq!(counter_b.get_value("write"), 2);
        assert_eq!(counter_a, counter_sum);
    }

    #[test]
    fn test_sub_counter() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 100);
        counter_a.set_value("write", 3);

        let mut counter_b = Counter::new();
        counter_b.set_value("read", 25);
        counter_b.set_value("write", 1);

        let mut counter_sum = Counter::new();
        counter_sum.set_value("read", 75);
        counter_sum.set_value("write", 2);

        counter_a.sub_counter(&counter_b);

        assert_eq!(counter_a.get_value("read"), 75);
        assert_eq!(counter_a.get_value("write"), 2);
        assert_eq!(counter_b.get_value("read"), 25);
        assert_eq!(counter_b.get_value("write"), 1);
        assert_eq!(counter_a, counter_sum);
    }

    #[test]
    fn test_counter_equality_nonzero() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 1);
        counter_a.set_value("write", 2);

        let mut counter_b = Counter::new();
        counter_b.set_value("read", 1);
        counter_b.set_value("write", 2);

        let mut counter_c = Counter::new();
        counter_c.set_value("read", 10);
        counter_c.set_value("write", 20);

        assert_eq!(counter_a, counter_b);
        assert_ne!(counter_a, counter_c);
        assert_ne!(counter_b, counter_c);

        let mut counter_d = Counter::new();
        counter_d.set_value("read", 1);
        counter_d.set_value("write", 2);
        counter_d.set_value("close", 1);
        counter_d.sub_value("close", 1);

        assert_eq!(counter_a, counter_d);
    }

    #[test]
    fn test_counter_equality_zero() {
        let mut counter_a = Counter::new();
        counter_a.set_value("read", 1);
        counter_a.set_value("write", 2);

        let mut counter_d = Counter::new();
        counter_d.set_value("read", 1);
        counter_d.set_value("write", 2);
        counter_d.set_value("close", 1);
        counter_d.sub_value("close", 1);

        assert_eq!(counter_a, counter_d);
    }

    #[test]
    fn test_to_string() {
        let mut counter = Counter::new();

        counter.add_one("read");
        counter.add_one("read");
        counter.add_one("close");
        counter.add_one("write");
        counter.add_one("write");
        counter.add_one("write");

        // Make sure the keys are sorted with the largest count first
        assert_eq!(
            counter.to_string(),
            String::from("{write:3, read:2, close:1}")
        );

        counter.add_one("read");
        counter.add_one("read");

        // The order should have changed with read first now
        assert_eq!(
            counter.to_string(),
            String::from("{read:4, write:3, close:1}")
        );
    }

    #[test]
    fn test_to_string_order() {
        let mut counter_a = Counter::new();
        counter_a.add_one("write");
        counter_a.add_one("close");
        counter_a.add_one("read");
        let mut counter_b = Counter::new();
        counter_b.add_one("read");
        counter_b.add_one("write");
        counter_b.add_one("close");

        // Make sure the counters of equal value are sorted based on key.
        assert_eq!(counter_a.to_string(), counter_b.to_string());
        assert_eq!(
            counter_a.to_string(),
            String::from("{close:1, read:1, write:1}")
        );
    }
}
