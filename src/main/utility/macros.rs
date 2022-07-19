/// Log a warning, and if a debug build then panic.
macro_rules! debug_panic {
    ($($x:tt)+) => {
        log::warn!($($x)+);
        #[cfg(debug_assertions)]
        panic!($($x)+);
    };
}

#[cfg(test)]
mod tests {
    // will panic in debug mode
    #[test]
    #[cfg(debug_assertions)]
    #[should_panic]
    fn debug_panic_macro() {
        debug_panic!("Hello {}", "World");
    }

    // will *not* panic in release mode
    #[test]
    #[cfg(not(debug_assertions))]
    fn debug_panic_macro() {
        debug_panic!("Hello {}", "World");
    }
}
