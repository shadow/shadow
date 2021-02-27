use crate::cshadow as c;
use log_bindings as c_log;

use log::{Level, LevelFilter, Log, Metadata, Record, SetLoggerError};
use std::{ffi, fmt};

static LOGGER: ShadowLogger = ShadowLogger {};

struct ShadowLogger {}

impl Log for ShadowLogger {
    fn enabled(&self, _metadata: &Metadata) -> bool {
        // in the init() function we set the level to LevelFilter::max()
        true
    }

    fn log(&self, record: &Record) {
        let log_level = match record.level() {
            Level::Error => c_log::_LogLevel_LOGLEVEL_ERROR,
            Level::Warn => c_log::_LogLevel_LOGLEVEL_WARNING,
            Level::Info => c_log::_LogLevel_LOGLEVEL_INFO,
            Level::Debug => c_log::_LogLevel_LOGLEVEL_DEBUG,
            Level::Trace => c_log::_LogLevel_LOGLEVEL_TRACE,
        };

        if unsafe { c::shadow_logger_shouldFilter(c::shadow_logger_getDefault(), log_level) } {
            return;
        }

        // allocate null-terminated strings
        let file = ffi::CString::new(record.file().unwrap_or("<none>")).unwrap();
        let module_path = ffi::CString::new(record.module_path().unwrap_or("<none>")).unwrap();
        let message = ffi::CString::new(fmt::format(*record.args())).unwrap();

        let line = record.line().unwrap_or(0u32) as i32;

        unsafe {
            c_log::logger_log(
                c_log::logger_getDefault(),
                log_level,
                file.as_ptr(),
                module_path.as_ptr(),
                line,
                b"%s\0".as_ptr() as *const i8,
                message.as_ptr(),
            );
        }
    }

    fn flush(&self) {
        unsafe { c_log::logger_flush(c_log::logger_getDefault()) };
    }
}

/// Initialize a logger which uses Shadow's C logging interface.
pub fn init() -> Result<(), SetLoggerError> {
    log::set_logger(&LOGGER)?;
    // log at all levels
    log::set_max_level(LevelFilter::max());
    Ok(())
}

mod export {
    use super::*;

    #[no_mangle]
    pub extern "C" fn rust_logging_init() {
        init().expect("Could not initialize logger");
    }
}
