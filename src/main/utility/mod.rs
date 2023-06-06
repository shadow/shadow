// defines macros, so must be included first
#[macro_use]
pub mod enum_passthrough;
#[macro_use]
pub mod macros;

pub mod byte_queue;
pub mod callback_queue;
pub mod childpid_watcher;
pub mod counter;
pub mod give;
pub mod interval_map;
pub mod legacy_callback_queue;
pub mod pcap_writer;
pub mod perf_timer;
pub mod proc_maps;
pub mod shm_cleanup;
pub mod sockaddr;
pub mod status_bar;
pub mod stream_len;
pub mod synchronization;
pub mod syscall;
pub mod time;

use std::ffi::CString;
use std::marker::PhantomData;
use std::os::unix::fs::{DirBuilderExt, MetadataExt};
use std::os::unix::prelude::OsStrExt;
use std::path::{Path, PathBuf};

use shadow_shim_helper_rs::HostId;

use crate::core::worker::Worker;
use crate::host::host::Host;

/// A pointer to an object that is safe to dereference from any thread,
/// *if* the Host lock for the specified host is held.
#[derive(Debug)]
pub struct HostTreePointer<T> {
    host_id: HostId,
    ptr: *mut T,
}

// We can't `derive` Copy and Clone without unnecessarily requiring
// T to be Copy and Clone. https://github.com/rust-lang/rust/issues/26925
impl<T> Copy for HostTreePointer<T> {}
impl<T> Clone for HostTreePointer<T> {
    fn clone(&self) -> Self {
        Self {
            host_id: self.host_id,
            ptr: self.ptr,
        }
    }
}

unsafe impl<T> Send for HostTreePointer<T> {}
unsafe impl<T> Sync for HostTreePointer<T> {}

impl<T> HostTreePointer<T> {
    /// Create a pointer that may only be accessed when the host with id
    /// `host_id` is active.
    pub fn new_for_host(host_id: HostId, ptr: *mut T) -> Self {
        Self { host_id, ptr }
    }

    /// Create a pointer that may only be accessed when the current host is
    /// active.
    pub fn new(ptr: *mut T) -> Self {
        let host_id = Worker::with_active_host(|h| h.info().id);
        Self::new_for_host(host_id.unwrap(), ptr)
    }

    /// Get the pointer.
    ///
    /// Panics if the configured host is not active.
    ///
    /// # Safety
    ///
    /// Pointer must only be dereferenced while the configures Host is
    /// still active, in addition to the normal safety requirements for
    /// dereferencing a pointer.
    pub unsafe fn ptr(&self) -> *mut T {
        // While a caller might conceivably get the pointer without the lock
        // held but only dereference after it actually is held, better to be
        // conservative here and try to catch mistakes.
        //
        // This function is still `unsafe` since it's now the caller's
        // responsibility to not release the lock and *then* dereference the
        // pointer.
        // SAFETY: caller's responsibility
        Worker::with_active_host(|h| unsafe { self.ptr_with_host(h) }).unwrap()
    }

    /// Get the pointer.
    ///
    /// Panics if `host` is not the one associated with `self`.
    ///
    /// # Safety
    ///
    /// Pointer must only be dereferenced while the configures Host is still
    /// active, in addition to the normal safety requirements for dereferencing
    /// a pointer.
    pub unsafe fn ptr_with_host(&self, host: &Host) -> *mut T {
        assert_eq!(self.host_id, host.info().id);
        self.ptr
    }

    /// Get the pointer without checking the active host.
    ///
    /// # Safety
    ///
    /// Pointer must only be dereferenced while the configures Host is still
    /// active, in addition to the normal safety requirements for dereferencing
    /// a pointer.
    pub unsafe fn ptr_unchecked(&self) -> *mut T {
        self.ptr
    }
}

/// A trait we can use as a compile-time check to make sure that an object is Send.
pub trait IsSend: Send {}

/// A trait we can use as a compile-time check to make sure that an object is Sync.
pub trait IsSync: Sync {}

/// Runtime memory error checking to help catch errors that C code is prone
/// to. Can probably drop once C interop is removed.
///
/// Prefer to place `Magic` struct fields as the *first* field.  This causes the
/// `Magic` field to be dropped first when dropping the enclosing struct, which
/// validates that the `Magic` is valid before running `Drop` implementations of
/// the other fields.
///
/// T should be the type of the struct that contains the Magic.
#[derive(Debug)]
pub struct Magic<T: 'static> {
    #[cfg(debug_assertions)]
    magic: std::any::TypeId,
    // The PhantomData docs recommend using `* const T` here to avoid a drop
    // check, but that would incorrectly make this type !Send and !Sync. As long
    // as the drop check doesn't cause issue (i.e. cause the borrow checker to
    // fail), it should be fine to just use T here.
    // https://doc.rust-lang.org/nomicon/dropck.html
    _phantom: PhantomData<T>,
}

impl<T> Magic<T> {
    pub fn new() -> Self {
        Self {
            #[cfg(debug_assertions)]
            magic: std::any::TypeId::of::<T>(),
            _phantom: PhantomData,
        }
    }

    pub fn debug_check(&self) {
        #[cfg(debug_assertions)]
        {
            if unsafe { std::ptr::read_volatile(&self.magic) } != std::any::TypeId::of::<T>() {
                // Do not pass Go; do not collect $200... and do not run Drop
                // implementations etc. after learning that Rust's soundness
                // requirements have likely been violated.
                std::process::abort();
            }
            // Ensure no other operations are performed on the object before validating.
            std::sync::atomic::compiler_fence(std::sync::atomic::Ordering::SeqCst);
        }
    }
}

impl<T> Default for Magic<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> Drop for Magic<T> {
    fn drop(&mut self) {
        self.debug_check();
        #[cfg(debug_assertions)]
        unsafe {
            std::ptr::write_volatile(&mut self.magic, std::any::TypeId::of::<()>())
        };
    }
}

impl<T> Clone for Magic<T> {
    fn clone(&self) -> Self {
        self.debug_check();
        Self::new()
    }
}

/// Helper for tracking the number of allocated objects.
#[derive(Debug)]
pub struct ObjectCounter {
    name: &'static str,
}

impl ObjectCounter {
    pub fn new(name: &'static str) -> Self {
        Worker::increment_object_alloc_counter(name);
        Self { name }
    }
}

impl Drop for ObjectCounter {
    fn drop(&mut self) {
        Worker::increment_object_dealloc_counter(self.name);
    }
}

impl Clone for ObjectCounter {
    fn clone(&self) -> Self {
        Worker::increment_object_alloc_counter(self.name);
        Self { name: self.name }
    }
}

pub fn tilde_expansion(path: &str) -> std::path::PathBuf {
    // if the path begins with a "~"
    if let Some(x) = path.strip_prefix('~') {
        // get the tilde-prefix (everything before the first separator)
        let (tilde_prefix, remainder) = x.split_once('/').unwrap_or((x, ""));

        if tilde_prefix.is_empty() {
            if let Ok(ref home) = std::env::var("HOME") {
                return [home, remainder].iter().collect::<std::path::PathBuf>();
            }
        } else if ['+', '-'].contains(&tilde_prefix.chars().next().unwrap()) {
            // not supported
        } else {
            return ["/home", tilde_prefix, remainder]
                .iter()
                .collect::<std::path::PathBuf>();
        }
    }

    // if we don't have a tilde-prefix that we support, just return the unmodified path
    std::path::PathBuf::from(path)
}

/// Copy the contents of the `src` directory to a new directory named `dst`. Permissions will be
/// preserved.
pub fn copy_dir_all(src: impl AsRef<Path>, dst: impl AsRef<Path>) -> std::io::Result<()> {
    // a directory to copy
    struct DirCopyTask {
        src: PathBuf,
        dst: PathBuf,
        mode: u32,
    }

    // a stack of directories to copy
    let mut stack: Vec<DirCopyTask> = vec![];

    stack.push(DirCopyTask {
        src: src.as_ref().to_path_buf(),
        dst: dst.as_ref().to_path_buf(),
        mode: src.as_ref().metadata()?.mode(),
    });

    while let Some(DirCopyTask { src, dst, mode }) = stack.pop() {
        // create the directory with the same permissions
        create_dir_with_mode(&dst, mode)?;

        // copy directory contents
        for entry in std::fs::read_dir(src)? {
            let entry = entry?;
            let meta = entry.metadata()?;
            let new_dst_path = dst.join(entry.file_name());

            if meta.is_dir() {
                stack.push(DirCopyTask {
                    src: entry.path(),
                    dst: new_dst_path,
                    mode: meta.mode(),
                });
            } else {
                // copy() will also copy the permissions
                std::fs::copy(entry.path(), &new_dst_path)?;
            }
        }
    }

    Ok(())
}

fn create_dir_with_mode(path: impl AsRef<Path>, mode: u32) -> std::io::Result<()> {
    let mut dir_builder = std::fs::DirBuilder::new();
    dir_builder.mode(mode);
    dir_builder.create(&path)
}

/// Helper for converting a PathBuf to a CString
pub fn pathbuf_to_nul_term_cstring(buf: PathBuf) -> CString {
    let mut bytes = buf.as_os_str().to_os_string().as_bytes().to_vec();
    bytes.push(0);
    CString::from_vec_with_nul(bytes).unwrap()
}

/// Get the return code for a process that exited by the given signal, following the behaviour of
/// bash.
pub fn return_code_for_signal(signal: nix::sys::signal::Signal) -> i32 {
    // bash adds 128 to to the signal
    (signal as i32).checked_add(128).unwrap()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_tilde_expansion() {
        if let Ok(ref home) = std::env::var("HOME") {
            assert_eq!(
                tilde_expansion("~/test"),
                [home, "test"].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("~"),
                [home].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("~/"),
                [home].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("~someuser/test"),
                ["/home", "someuser", "test"]
                    .iter()
                    .collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion("/~/test"),
                ["/", "~", "test"].iter().collect::<std::path::PathBuf>()
            );

            assert_eq!(
                tilde_expansion(""),
                [""].iter().collect::<std::path::PathBuf>()
            );
        }
    }
}

mod export {
    #[no_mangle]
    pub unsafe extern "C" fn utility_handleErrorInner(
        file_name: *const libc::c_char,
        line: libc::c_int,
        fn_name: *const libc::c_char,
        format: *const libc::c_char,
        va_list: *mut libc::c_void,
    ) -> ! {
        use std::ffi::CStr;
        let file_name = unsafe { CStr::from_ptr(file_name) };
        let file_name = file_name.to_bytes().escape_ascii();

        let fn_name = unsafe { CStr::from_ptr(fn_name) };
        let fn_name = fn_name.to_bytes().escape_ascii();

        log::logger().flush();

        let indent = "    ";

        // add four spaces at the start of every line
        let backtrace = format!("{:?}", backtrace::Backtrace::new());
        let backtrace = backtrace
            .trim_end()
            .split('\n')
            .map(|x| format!("{indent}{x}"))
            .collect::<Vec<String>>()
            .join("\n");

        let pid = nix::unistd::getpid();
        let ppid = nix::unistd::getppid();

        let error_msg = unsafe { vsprintf::vsprintf_raw(format, va_list).unwrap() };
        let error_msg = error_msg.escape_ascii();

        let error_msg = format!(
            "**ERROR ENCOUNTERED**\n\
              {indent}At process: {pid} (parent {ppid})\n\
              {indent}At file: {file_name}\n\
              {indent}At line: {line}\n\
              {indent}At function: {fn_name}\n\
              {indent}Message: {error_msg}\n\
            **BEGIN BACKTRACE**\n\
            {backtrace}\n\
            **END BACKTRACE**\n\
            **ABORTING**"
        );

        if !nix::unistd::isatty(libc::STDOUT_FILENO).unwrap_or(true) {
            println!("{error_msg}");
        }

        eprintln!("{error_msg}");

        std::process::abort()
    }
}
