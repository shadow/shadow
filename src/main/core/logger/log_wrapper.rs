use std::os::raw::{c_char, c_int, c_void};

use log::log_enabled;
use vsprintf::vsprintf_raw;

/// Flush Rust's log::logger().
#[no_mangle]
pub extern "C" fn rustlogger_flush() {
    log::logger().flush();
}

/// Returns the `str` pointed to by `ptr` if it's non-NULL and points
/// to a UTF-8 null-terminated string.
/// # Safety
///
/// `ptr` must point a NULL-terminated C String if non-null, and must
/// be immutable for the lifetime of the returned `str`.
unsafe fn optional_str(ptr: *const c_char) -> Option<&'static str> {
    if ptr.is_null() {
        None
    } else {
        // Safe if caller obeyed doc'd preconditions.
        unsafe { std::ffi::CStr::from_ptr(ptr).to_str().ok() }
    }
}

pub fn c_to_rust_log_level(level: logger::LogLevel) -> Option<log::Level> {
    use log::Level::*;
    match level {
        logger::_LogLevel_LOGLEVEL_ERROR => Some(Error),
        logger::_LogLevel_LOGLEVEL_WARNING => Some(Warn),
        logger::_LogLevel_LOGLEVEL_INFO => Some(Info),
        logger::_LogLevel_LOGLEVEL_DEBUG => Some(Debug),
        logger::_LogLevel_LOGLEVEL_TRACE => Some(Trace),
        logger::_LogLevel_LOGLEVEL_UNSET => None,
        _ => panic!("Unexpected log level {}", level),
    }
}

/// Whether logging is currently enabled for `level`.
#[no_mangle]
pub extern "C" fn rustlogger_isEnabled(level: logger::LogLevel) -> c_int {
    let level = c_to_rust_log_level(level).unwrap();
    log_enabled!(level).into()
}

/// Log to Rust's log::logger().
///
/// # Safety
///
/// Pointer args must be safely dereferenceable.
#[no_mangle]
pub unsafe extern "C" fn rustlogger_log(
    level: logger::LogLevel,
    file_name: *const c_char,
    fn_name: *const c_char,
    line: i32,
    format: *const c_char,
    va_list: *mut c_void,
) {
    let log_level = c_to_rust_log_level(level).unwrap();

    if !log_enabled!(log_level) {
        return;
    }

    // SAFETY: Safe if caller provided valid format and va_list.
    let msg_vec = unsafe { vsprintf_raw(format, va_list).unwrap() };
    let msg = String::from_utf8_lossy(&msg_vec);

    log::logger().log(
        &log::Record::builder()
            .level(log_level)
            // SAFETY: file_name is statically allocated.
            .file_static(unsafe { optional_str(file_name) })
            .line(Some(u32::try_from(line).unwrap()))
            // SAFETY: fn_name is statically allocated.
            .module_path_static(unsafe { optional_str(fn_name) })
            .args(format_args!("{}", msg))
            .build(),
    );
}
