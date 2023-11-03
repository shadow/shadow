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

use std::collections::HashSet;
use std::ffi::CString;
use std::marker::PhantomData;
use std::os::unix::fs::{DirBuilderExt, MetadataExt};
use std::os::unix::prelude::OsStrExt;
use std::path::{Path, PathBuf};
use std::sync::RwLock;

use once_cell::sync::Lazy;
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
        *self
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

/// Convert a `&[u8]` to `&[i8]`. Useful when interacting with C strings. Panics if
/// `i8::try_from(c)` fails for any `c` in the slice.
pub fn u8_to_i8_slice(s: &[u8]) -> &[i8] {
    // assume that if try_from() was successful, then a direct cast would also be
    assert!(s.iter().all(|x| i8::try_from(*x).is_ok()));
    unsafe { std::slice::from_raw_parts(s.as_ptr() as *const i8, s.len()) }
}

/// Convert a `&[i8]` to `&[u8]`. Useful when interacting with C strings. Panics if
/// `u8::try_from(c)` fails for any `c` in the slice.
pub fn i8_to_u8_slice(s: &[i8]) -> &[u8] {
    // assume that if try_from() was successful, then a direct cast would also be
    assert!(s.iter().all(|x| u8::try_from(*x).is_ok()));
    unsafe { std::slice::from_raw_parts(s.as_ptr() as *const u8, s.len()) }
}

#[derive(Debug)]
pub enum VerifyPluginPathError {
    NotFound,
    // Not a file.
    NotFile,
    // File isn't executable.
    NotExecutable,
    // File isn't a dynamically linked ELF.
    // TODO: split these errors, and/or support `#!` interpreters?
    NotDynamicallyLinkedElf,
    // Permission denied traversing the path.
    PathPermissionDenied,
    UnhandledIoError(std::io::Error),
}
impl std::error::Error for VerifyPluginPathError {}

impl std::fmt::Display for VerifyPluginPathError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            VerifyPluginPathError::NotFound => f.write_str("path not found"),
            VerifyPluginPathError::NotFile => f.write_str("not a file"),
            VerifyPluginPathError::NotExecutable => f.write_str("not executable"),
            VerifyPluginPathError::NotDynamicallyLinkedElf => {
                f.write_str("not a dynamically linked ELF")
            }
            VerifyPluginPathError::PathPermissionDenied => {
                f.write_str("permission denied traversing path")
            }
            VerifyPluginPathError::UnhandledIoError(e) => write!(f, "unhandled io error: {e}"),
        }
    }
}

/// Check that the plugin path is executable under Shadow.
pub fn verify_plugin_path(path: impl AsRef<std::path::Path>) -> Result<(), VerifyPluginPathError> {
    let path = path.as_ref();

    let metadata = std::fs::metadata(path).map_err(|e| {
        match e.kind() {
            std::io::ErrorKind::NotFound => VerifyPluginPathError::NotFound,
            std::io::ErrorKind::PermissionDenied => VerifyPluginPathError::PathPermissionDenied,
            // TODO handle TooManyLinks when stabilized
            // TODO handle InvalidFileName when stabilized
            k => {
                log::warn!("Unhandled error getting metadata for {path:?}: {k:?}");
                VerifyPluginPathError::UnhandledIoError(e)
            }
        }
    })?;

    if !metadata.is_file() {
        return Err(VerifyPluginPathError::NotFile);
    }

    // this mask doesn't guarantee that we can execute the file (the file might have S_IXUSR
    // but be owned by a different user), but it should catch most errors
    let mask = libc::S_IXUSR | libc::S_IXGRP | libc::S_IXOTH;
    if (metadata.mode() & mask) == 0 {
        return Err(VerifyPluginPathError::NotExecutable);
    }

    // a cache so we don't check the same path multiple times (assuming the user doesn't move any
    // binaries while shadow is running)
    // TODO: maybe move this into `sim_config.rs`? This seems slightly more
    // possible to go stale for paths exec'd by managed code.
    static CHECKED_DYNAMIC_BINS: Lazy<RwLock<HashSet<PathBuf>>> =
        Lazy::new(|| RwLock::new(HashSet::new()));

    let is_known_dynamic = CHECKED_DYNAMIC_BINS.read().unwrap().contains(path);

    // check if the binary is dynamically linked
    if !is_known_dynamic {
        let ld_path = "/lib64/ld-linux-x86-64.so.2";
        let ld_output = std::process::Command::new(ld_path)
            .arg("--verify")
            .arg(path)
            .output()
            .expect("Unable to run '{ld_path}'");

        if ld_output.status.success() {
            CHECKED_DYNAMIC_BINS
                .write()
                .unwrap()
                .insert(path.to_path_buf());
        } else {
            log::debug!("ld stderr: {:?}", ld_output.stderr);
            // technically ld-linux could return errors for other reasons, but this is the most
            // likely reason given that we already checked that the file exists
            return Err(VerifyPluginPathError::NotDynamicallyLinkedElf);
        }
    }

    Ok(())
}

/// Inject `injected_preloads` into the environment `envv`.
///
/// * Ordering of `envv` is preserved.
/// * Ordering of preloads already in `envv` is preserved.
/// * Addition of duplicate entries from `injected_preloads` is suppressed (to avoid
///   unbounded growth of env through chain of execve's)
pub fn inject_preloads(mut envv: Vec<CString>, injected_preloads: &[PathBuf]) -> Vec<CString> {
    let ld_preload_key = CString::new("LD_PRELOAD=").unwrap();

    let ld_preload_kv;
    if let Some(kv) = envv
        .iter_mut()
        .find(|v| v.to_bytes().starts_with(ld_preload_key.as_bytes()))
    {
        // We found an existing LD_PRELOAD definition, so we'll mutate it.  In
        // the (unusual) case that LD_PRELOAD is defined multiple times, the
        // first is the one that will be effective; we mutate that one and
        // ignore the others.
        ld_preload_kv = kv;
    } else {
        // No existing LD_PRELOAD definition; add an empty one.
        envv.push(ld_preload_key.clone());
        ld_preload_kv = envv.last_mut().unwrap();
    }

    let previous_preloads_string = ld_preload_kv
        .as_bytes()
        .strip_prefix(ld_preload_key.as_bytes())
        .unwrap();

    let injected_preloads_bytes = injected_preloads
        .iter()
        .map(|path| path.as_os_str().as_bytes());

    for p in injected_preloads_bytes.clone() {
        // Should have been caught earlier in configuraton parsing,
        // but verify here at point of use.
        assert!(
            !p.iter().any(|c| *c == b' ' || *c == b':'),
            "Preload path contains LD_PRELOAD separator"
        );
    }

    // `ld.so(8)`: The items of the list can be separated by spaces or colons,
    // and there is no support for escaping either separator.
    let previous_preloads = previous_preloads_string.split(|c| *c == b':' || *c == b' ');

    // Deduplicate. e.g. in the case where one managed process exec's another
    // and passes in its own environment to the child, we don't want to add
    // duplicates here.
    let filtered_previous_preloads =
        previous_preloads.filter(|p| !injected_preloads_bytes.clone().any(|q| &q == p));

    let injected_preloads_bytes = injected_preloads
        .iter()
        .map(|path| path.as_os_str().as_bytes());

    let mut preloads = injected_preloads_bytes.chain(filtered_previous_preloads);

    // Some way to use `join` here? I couldn't work out a nice way.
    let mut output = Vec::<u8>::new();
    output.extend(ld_preload_key.as_bytes());
    // Insert first entry without a separator
    if let Some(p) = preloads.next() {
        output.extend(p);
    }
    // Add the rest with separators
    for preload in preloads {
        output.push(b':');
        output.extend(preload);
    }

    // We could probably safely use an unchecked CString constructor here, but
    // probably not worth the risk of a subtle bug.
    *ld_preload_kv = CString::new(output).unwrap();

    envv
}

/// If debug assertions are enabled, panics if `FD_CLOEXEC` is not set on `file`.
///
/// In shadow we want `FD_CLOEXEC` set on most files that we create, to avoid them leaking
/// into subprocesses that we spawn. Rust's file APIs typically set this in practice,
/// but don't formally guarantee it. It's unlikely that they'd ever not set it, but we'd
/// like to know if that happens.
///
/// The likely result of it not being set is just file descriptors leaking into
/// subprocesses.  This counts against kernel limits against the total number
/// of file descriptors, and may cause the underlying file description to remain
/// open longer than needed. Theoretically the subprocess could also operate on
/// the leaked descriptor, causing difficult-to-diagnose issues, but this is
/// unlikely in practice, especially since shadow's shim should prevent any
/// native file operations from being executed from managed code in the first place.
pub fn debug_assert_cloexec(file: &(impl std::os::fd::AsRawFd + std::fmt::Debug)) {
    #[cfg(debug_assertions)]
    {
        let flags = nix::fcntl::fcntl(file.as_raw_fd(), nix::fcntl::FcntlArg::F_GETFD).unwrap();
        let flags = nix::fcntl::FdFlag::from_bits_retain(flags);
        debug_assert!(flags.contains(nix::fcntl::FdFlag::FD_CLOEXEC), "{file:?} is unexpectedly not FD_CLOEXEC, which may lead to resource leaks or strange behavior");
    }
    #[cfg(not(debug_assertions))]
    {
        // Silence unused variable warning
        let _ = file;
    }
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

    #[test]
    fn test_inject_preloads() {
        // Base case
        assert_eq!(
            inject_preloads(vec![], &[]),
            vec![CString::new("LD_PRELOAD=").unwrap()]
        );

        // Other env vars are preserved
        assert_eq!(
            inject_preloads(
                vec![
                    CString::new("foo=foo").unwrap(),
                    CString::new("bar=bar").unwrap(),
                ],
                &[]
            ),
            vec![
                CString::new("foo=foo").unwrap(),
                CString::new("bar=bar").unwrap(),
                CString::new("LD_PRELOAD=").unwrap()
            ]
        );

        // Prefixes existing preloads
        assert_eq!(
            inject_preloads(
                vec![CString::new("LD_PRELOAD=/existing.so").unwrap()],
                &[PathBuf::from("/injected.so")]
            ),
            vec![CString::new("LD_PRELOAD=/injected.so:/existing.so").unwrap()]
        );

        // Doesn't duplicate
        assert_eq!(
            inject_preloads(
                vec![CString::new("LD_PRELOAD=/injected.so").unwrap()],
                &[PathBuf::from("/injected.so")]
            ),
            &[CString::new("LD_PRELOAD=/injected.so").unwrap()]
        );

        // Multiple existing, multiple injected, partial dedupe
        assert_eq!(
            inject_preloads(
                vec![
                    CString::new("foo=foo").unwrap(),
                    CString::new("LD_PRELOAD=/existing1.so:/injected1.so:/existing2.so").unwrap(),
                    CString::new("bar=bar").unwrap()
                ],
                &[
                    PathBuf::from("/injected1.so"),
                    PathBuf::from("/injected2.so"),
                ],
            ),
            &[
                CString::new("foo=foo").unwrap(),
                CString::new("LD_PRELOAD=/injected1.so:/injected2.so:/existing1.so:/existing2.so")
                    .unwrap(),
                CString::new("bar=bar").unwrap(),
            ]
        );
    }
}

mod export {
    use std::io::IsTerminal;

    #[no_mangle]
    pub unsafe extern "C-unwind" fn utility_handleErrorInner(
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

        eprintln!("{error_msg}");

        // If stderr is a terminal, and stdout isn't, also print to stdout.
        // This helps ensure the error is preserved in the case that stdout
        // is recorded to a file but stderr is not.
        if std::io::stderr().lock().is_terminal() && !std::io::stdout().lock().is_terminal() {
            println!("{error_msg}");
        }

        std::process::abort()
    }
}
