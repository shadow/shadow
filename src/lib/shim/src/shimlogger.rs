use core::fmt::Write;
use core::time::Duration;

use formatting_nostd::{BorrowedFdWriter, FormatBuffer};
use rustix::fd::BorrowedFd;
use shadow_shim_helper_rs::util::time::TimeParts;

/// For internal use; writes to an internal buffer, flushing to stdout
/// when the buffer fills or the object is dropped.
struct ShimLoggerWriter {
    buffer: FormatBuffer<1000>,
    stdout: BorrowedFdWriter<'static>,
}

impl ShimLoggerWriter {
    pub fn new() -> Self {
        Self {
            buffer: FormatBuffer::new(),
            stdout: BorrowedFdWriter::new(unsafe {
                BorrowedFd::borrow_raw(linux_raw_sys::general::STDOUT_FILENO.try_into().unwrap())
            }),
        }
    }

    pub fn flush(&mut self) {
        self.stdout.write_str(self.buffer.as_str()).unwrap();
        self.buffer.reset();
    }
}

impl core::fmt::Write for ShimLoggerWriter {
    fn write_str(&mut self, s: &str) -> Result<(), core::fmt::Error> {
        if s.len() > self.buffer.capacity_remaining() {
            // Flush to make room and keep output FIFO.
            self.flush();
        }
        if s.len() > self.buffer.capacity_remaining() {
            // There will never be enough room. Write directly to stdout.
            self.stdout.write_str(s)
        } else {
            // Write to buffer. Should be impossible to fail.
            self.buffer.write_str(s)
        }
    }
}

impl Drop for ShimLoggerWriter {
    fn drop(&mut self) {
        self.flush();
    }
}

/// Implementation of `log::Log` for use in the shim.
///
/// Includes some shim related metadata (such as simulation time), is no_std,
/// and is careful to only make inlined syscalls to avoid getting intercepted by
/// the shim's seccomp filter.
pub struct ShimLogger {}

impl ShimLogger {
    /// Install a `ShimLogger` as  the logging backend in the Rust `log` crate.
    /// Should only be called once.
    pub fn install(log_level: log::LevelFilter) {
        // log::set_logger requires a logger with a static lifetime.
        const SHIM_LOGGER: ShimLogger = ShimLogger {};

        log::set_max_level(log_level);
        log::set_logger(&SHIM_LOGGER).unwrap();
    }
}

impl log::Log for ShimLogger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= log::max_level()
    }

    fn log(&self, record: &log::Record) {
        if !self.enabled(record.metadata()) {
            return;
        }

        let mut writer = ShimLoggerWriter::new();

        match crate::global_manager_shmem::try_get() {
            Some(m) => {
                // rustix's `clock_gettime` goes through VDSO, which is overwritten with our trampoline,
                // which would end up trying to log and recurse.
                // `linux_api`'s `clock_gettime` always makes the syscall, which is what we want.
                let now = linux_api::time::clock_gettime(linux_api::time::ClockId::CLOCK_REALTIME)
                    .unwrap();

                let now = Duration::from_secs(now.tv_sec.try_into().unwrap())
                    + Duration::from_nanos(now.tv_nsec.try_into().unwrap());

                let start = Duration::from_micros(m.log_start_time_micros.try_into().unwrap());
                let elapsed = now - start;
                let parts = TimeParts::from_nanos(elapsed.as_nanos());
                write!(&mut writer, "{} ", parts.fmt_hr_min_sec_nano(),).unwrap();
            }
            None => {
                writer.write_str("? ").unwrap();
            }
        }

        match crate::simtime() {
            Some(t) => {
                let t = Duration::from(t);
                let parts = TimeParts::from_nanos(t.as_nanos());
                write!(&mut writer, "[{}] ", parts.fmt_hr_min_sec_nano(),).unwrap();
            }
            None => {
                writer.write_str("[?] ").unwrap();
            }
        };

        write!(
            &mut writer,
            "[shd-shim] [{level}] [{file_name}:{line_number}] [{function_name}] ",
            level = record.level(),
            file_name = record.file().unwrap_or("?"),
            line_number = record.line().unwrap_or(0),
            function_name = record.module_path().unwrap_or("?"),
        )
        .unwrap();
        core::fmt::write(&mut writer, *record.args()).unwrap();
        writer.write_char('\n').unwrap();
    }

    fn flush(&self) {}
}

pub mod export {
    use super::*;

    #[unsafe(no_mangle)]
    pub extern "C-unwind" fn shimlogger_install(level: logger::LogLevel) {
        let level = log_c2rust::c_to_rust_log_level(level).unwrap();
        ShimLogger::install(level.to_level_filter());
    }
}
