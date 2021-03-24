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
use std::fmt::{Display, Formatter, Result};
use std::iter::FromIterator;

/// The main counter object that maps individual keys to count values.
pub struct Counter {
    // TODO: convert this so we could count generic types instead of Strings
    items: HashMap<String, u64>,
}

impl Counter {
    /// Initializes a new counter map that starts with no keys.
    pub fn new() -> Counter {
        Counter {
            items: HashMap::new(),
        }
    }

    /// Increment the counter value for the key given by id.
    /// Returns the value of the counter after it was incremented.
    pub fn add_one(&mut self, id: &str) -> u64 {
        match self.items.get_mut(id) {
            Some(val) => {
                // Increment and return the existing value without allocating new key
                *val = *val + 1;
                *val
            }
            None => {
                // Allocate new key and insert it with initial count of 1
                assert_eq!(self.items.insert(id.to_string(), 1), None);
                1
            }
        }
    }

    /// Returns the counter value for the key given by id, or 0 if the key has not
    /// previously been incremented.
    pub fn get_value(&mut self, id: &str) -> u64 {
        match self.items.get(&id.to_string()) {
            Some(val) => *val,
            None => 0,
        }
    }
}

impl Display for Counter {
    /// Returns a string representation of the counter in the form
    ///   `{key1:value1, key2:value2, ..., keyN:valueN}`
    /// for known keys and values, where the list is sorted by value with the
    /// largest value first.
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        // Sort the counts with the heaviest hitters first
        let mut item_vec = Vec::from_iter(&self.items);
        item_vec.sort_by(|&(_, a), &(_, b)| b.cmp(&a));

        // Create a string representation of the counts by iterating over the items.
        write!(f, "{{")?;
        for i in 0..item_vec.len() {
            write!(f, "{}:{}", item_vec[i].0, item_vec[i].1)?;
            if i < (item_vec.len() - 1) {
                write!(f, ", ")?;
            }
        }
        write!(f, "}}")
    }
}

mod export {
    use super::*;
    use std::ffi::CStr;
    use std::ffi::CString;
    use std::os::raw::c_char;

    #[no_mangle]
    pub extern "C" fn counter_new() -> *mut Counter {
        Box::into_raw(Box::new(Counter::new()))
    }

    #[no_mangle]
    pub extern "C" fn counter_free(counter_ptr: *mut Counter) {
        if counter_ptr.is_null() {
            return;
        }
        unsafe {
            Box::from_raw(counter_ptr);
        }
    }

    #[no_mangle]
    pub extern "C" fn counter_add_one(counter: *mut Counter, id: *const c_char) -> u64 {
        assert!(!counter.is_null());
        assert!(!id.is_null());

        let counter = unsafe { &mut *counter };
        let id = unsafe { CStr::from_ptr(id) };

        counter.add_one(id.to_str().unwrap())
    }

    /// Creates a new string representation of the counter, e.g., for logging.
    /// The returned string must be free'd by passing it to counter_free_string.
    #[no_mangle]
    pub extern "C" fn counter_alloc_string(counter: *mut Counter) -> *mut c_char {
        assert!(!counter.is_null());

        let counter = unsafe { &mut *counter };
        let string = counter.to_string();

        // Transfer ownership back to caller
        CString::new(string).unwrap().into_raw()
    }

    /// Frees a string previously returned from counter_alloc_string.
    #[no_mangle]
    pub extern "C" fn counter_free_string(counter: *mut Counter, ptr: *mut c_char) {
        assert!(!counter.is_null());
        assert!(!ptr.is_null());
        // Free the previously alloc'd string
        unsafe { CString::from_raw(ptr) };
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
}
