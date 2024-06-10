/// Log a warning, and if a debug build then panic.
macro_rules! debug_panic {
    ($($x:tt)+) => {
        log::warn!($($x)+);
        #[cfg(debug_assertions)]
        panic!($($x)+);
    };
}

/// Log a message once at level `lvl_once`, and any later log messages from this line at level
/// `lvl_remaining`. A log target is not supported. The string "(LOG_ONCE)" will be prepended to the
/// message to indicate that future messages won't be logged at `lvl_once`.
///
/// ```
/// # use log::Level;
/// # use shadow_rs::log_once_at_level;
/// log_once_at_level!(Level::Warn, Level::Debug, "Unexpected flag {}", 10);
/// ```
#[allow(unused_macros)]
#[macro_export]
macro_rules! log_once_at_level {
    ($lvl_once:expr, $lvl_remaining:expr, $str:literal $($x:tt)*) => {
        // don't do atomic operations if this log statement isn't enabled
        if log::log_enabled!($lvl_once) || log::log_enabled!($lvl_remaining) {
            static HAS_LOGGED: std::sync::atomic::AtomicBool =
                std::sync::atomic::AtomicBool::new(false);

            // TODO: doing just a `load()` might be faster in the typical case, but would need to
            // have performance metrics to back that up
            match HAS_LOGGED.compare_exchange(
                false,
                true,
                std::sync::atomic::Ordering::Relaxed,
                std::sync::atomic::Ordering::Relaxed,
            ) {
                Ok(_) => log::log!($lvl_once, "(LOG_ONCE) {}", format_args!($str $($x)*)),
                Err(_) => log::log!($lvl_remaining, "(LOG_ONCE) {}", format_args!($str $($x)*)),
            }
        }
    };
}

/// Log a message once at level `lvl_once` for each distinct value, and any
/// later log messages from this line with an already-logged value at level
/// `lvl_remaining`. A log target is not supported. The string "(LOG_ONCE)" will
/// be prepended to the message to indicate that future messages won't be logged
/// at `lvl_once`.
///
/// The fast-path (where the given value has already been logged) aquires a
/// read-lock and looks up the value in a hash table.
///
/// ```
/// # use log::Level;
/// # use shadow_rs::log_once_per_value_at_level;
/// # let unknown_flag: i32 = 0;
/// log_once_per_value_at_level!(unknown_flag, i32, Level::Warn, Level::Debug, "Unknown flag value {unknown_flag}");
/// ```
#[allow(unused_macros)]
#[macro_export]
macro_rules! log_once_per_value_at_level {
    ($value:expr, $t:ty, $lvl_once:expr, $lvl_remaining:expr, $str:literal $($x:tt)*) => {
        // don't do atomic operations if this log statement isn't enabled
        if log::log_enabled!($lvl_once) || log::log_enabled!($lvl_remaining) {
            use $crate::utility::once_set::OnceSet;
            static LOGGED_SET : OnceSet<$t> = OnceSet::new();

            let level = if LOGGED_SET.insert($value) {
                $lvl_once
            } else {
                $lvl_remaining
            };
            log::log!(level, "(LOG_ONCE) {}", format_args!($str $($x)*))
        }
    };
}

/// Log a message once at warn level, and any later log messages from this line at debug level. A
/// log target is not supported. The string "(LOG_ONCE)" will be prepended to the message to
/// indicate that future messages won't be logged at warn level.
///
/// ```ignore
/// warn_once_then_debug!("Unexpected flag {}", 10);
/// ```
#[allow(unused_macros)]
macro_rules! warn_once_then_debug {
    ($($x:tt)+) => {
        log_once_at_level!(log::Level::Warn, log::Level::Debug, $($x)+);
    };
}

/// Log a message once at warn level, and any later log messages from this line at trace level. A
/// log target is not supported. The string "(LOG_ONCE)" will be prepended to the message to
/// indicate that future messages won't be logged at warn level.
///
/// ```ignore
/// warn_once_then_trace!("Unexpected flag {}", 10);
/// ```
#[allow(unused_macros)]
macro_rules! warn_once_then_trace {
    ($($x:tt)+) => {
        log_once_at_level!(log::Level::Warn, log::Level::Trace, $($x)+);
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

    #[test]
    fn log_once_at_level() {
        // we don't have a logger set up so we can't actually inspect the log output (well we
        // probably could with a custom logger), so instead we just make sure it compiles
        for x in 0..10 {
            log_once_at_level!(log::Level::Warn, log::Level::Debug, "{x}");
        }

        log_once_at_level!(log::Level::Warn, log::Level::Debug, "A");
        log_once_at_level!(log::Level::Warn, log::Level::Debug, "A");

        // expected log output is:
        // Warn: 0
        // Debug: 1
        // Debug: 2
        // ...
        // Warn: A
        // Warn: A
    }

    #[test]
    fn warn_once() {
        warn_once_then_trace!("A");
        warn_once_then_debug!("A");
    }
}
