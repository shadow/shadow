/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
use std::collections::HashMap;
use std::iter::FromIterator;

pub struct Counter {
    items: HashMap<String, u64>,
}

impl Counter {
    pub fn new() -> Counter {
        Counter {
            items: HashMap::new(),
        }
    }

    pub fn add_one(&mut self, id: &str) -> u64 {
        let count = self.items.entry(id.to_string()).or_insert(0);
        *count = *count + 1;
        *count
    }

    // Used for unit test
    #[allow(dead_code)]
    pub fn get_value(&mut self, id: &str) -> u64 {
        match self.items.get(&id.to_string()) {
            Some(val) => *val,
            None => 0,
        }
    }

    fn to_string(&mut self) -> String {
        // Sort the counts with the heaviest hitters first
        let mut item_vec = Vec::from_iter(&self.items);
        item_vec.sort_by(|&(_, a), &(_, b)| b.cmp(&a));

        // Create a string representation of the counts
        let mut string = String::from("Counts:");
        for item in item_vec.iter() {
            string.push_str(format!(" {}={}", item.0, item.1).as_str());
        }
        string
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

    #[no_mangle]
    pub extern "C" fn counter_alloc_string(counter: *mut Counter) -> *mut c_char {
        assert!(!counter.is_null());

        let counter = unsafe { &mut *counter };
        let string = counter.to_string();

        // Transfer ownership back to caller
        CString::new(string).unwrap().into_raw()
    }

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
            String::from("Counts: write=3 read=2 close=1")
        );

        counter.add_one("read");
        counter.add_one("read");

        // The order should have changed with read first now
        assert_eq!(
            counter.to_string(),
            String::from("Counts: read=4 write=3 close=1")
        );
    }
}
