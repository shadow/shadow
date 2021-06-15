/// Panics if NULL (when debug asserts are enabled), and returns the pointer.
pub fn notnull_debug<T>(p: *const T) -> *const T {
    debug_assert!(!p.is_null());
    p
}

/// Panics if NULL (when debug asserts are enabled), and returns the pointer.
pub fn notnull_mut_debug<T>(p: *mut T) -> *mut T {
    debug_assert!(!p.is_null());
    p
}

/// Panics if NULL and returns the pointer.
pub fn notnull<T>(p: *const T) -> *const T {
    assert!(!p.is_null());
    p
}

/// Panics if NULL and returns the pointer.
pub fn notnull_mut<T>(p: *mut T) -> *mut T {
    assert!(!p.is_null());
    p
}
