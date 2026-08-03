/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

//! Utilities helpful for writing Rust integration tests.

use std::collections::HashSet;
use std::io::{PipeReader, Write};
use std::marker::PhantomData;
use std::os::fd::AsRawFd as _;
use std::sync::mpsc;
use std::time::{Duration, SystemTime};
use std::{fmt, thread};

use nix::poll::PollFlags;
use nix::sys::signal;
use nix::sys::time::TimeVal;
use shadow_pod::ReadExt as _;

pub mod socket_utils;
pub mod time;

#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash)]
pub enum TestEnvironment {
    Shadow,
    Libc,
}

pub struct ShadowTest<T, E> {
    name: String,
    func: Box<dyn Fn() -> Result<T, E>>,
    passing: HashSet<TestEnvironment>,
}

impl<T, E> fmt::Debug for ShadowTest<T, E> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ShadowTest")
            .field("name", &self.name)
            .field("passing", &self.passing)
            .finish()
    }
}

impl<T, E> ShadowTest<T, E> {
    pub fn new(
        name: &str,
        func: impl Fn() -> Result<T, E> + 'static,
        passing: HashSet<TestEnvironment>,
    ) -> Self {
        Self {
            name: name.to_string(),
            func: Box::new(func),
            passing,
        }
    }

    pub fn run(&self) -> Result<T, E> {
        (self.func)()
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn passing(&self, environment: TestEnvironment) -> bool {
        self.passing.contains(&environment)
    }
}

/// Runs provided tests until failure and outputs results to stdout.
pub fn run_tests<'a, I, T: 'a, E>(tests: I, summarize: bool) -> Result<Vec<T>, E>
where
    I: IntoIterator<Item = &'a ShadowTest<T, E>>,
    E: 'a + std::fmt::Debug + std::fmt::Display,
{
    let mut results = vec![];

    for test in tests {
        print!("Testing {}...", test.name());
        std::io::stdout().flush().unwrap();

        match test.run() {
            Err(failure) => {
                println!(" ✗ ({failure})");
                if !summarize {
                    return Err(failure);
                }
            }
            Ok(result) => {
                results.push(result);
                println!(" ✓");
            }
        }
    }

    Ok(results)
}

// AsPtr and AsMutPtr traits inspired by https://stackoverflow.com/q/35885670

/// An object that can be converted to a pointer (possibly null).
pub trait AsPtr<T> {
    fn as_ptr(&self) -> *const T;
}

impl<T> AsPtr<T> for Option<T> {
    fn as_ptr(&self) -> *const T {
        match self {
            Some(v) => std::ptr::from_ref(v),
            None => std::ptr::null(),
        }
    }
}

/// An object that can be converted to a mutable pointer (possibly null).
pub trait AsMutPtr<T> {
    fn as_mut_ptr(&mut self) -> *mut T;
}

impl<T> AsMutPtr<T> for Option<T> {
    fn as_mut_ptr(&mut self) -> *mut T {
        match self {
            Some(v) => std::ptr::from_mut(v),
            None => std::ptr::null_mut(),
        }
    }
}

/// Return the error message if the condition is false.
pub fn result_assert(cond: bool, message: &str) -> Result<(), String> {
    if cond {
        Ok(())
    } else {
        Err(message.to_string())
    }
}

/// Return a formatted error message if `a` and `b` are unequal.
pub fn result_assert_eq<T>(a: T, b: T, message: &str) -> Result<(), String>
where
    T: std::fmt::Debug + std::cmp::PartialEq,
{
    if a == b {
        Ok(())
    } else {
        Err(format!("{a:?} != {b:?} -- {message}"))
    }
}

/// Return a formatted error message if `a` and `b` are equal.
pub fn result_assert_ne<T>(a: T, b: T, message: &str) -> Result<(), String>
where
    T: std::fmt::Debug + std::cmp::PartialEq,
{
    if a != b {
        Ok(())
    } else {
        Err(format!("{a:?} == {b:?} -- {message}"))
    }
}

/// Return a formatted error message if `a < b` is not true.
pub fn result_assert_lt<T>(a: T, b: T, message: &str) -> Result<(), String>
where
    T: std::fmt::Debug + std::cmp::PartialOrd,
{
    if a < b {
        Ok(())
    } else {
        Err(format!("!({a:?} < {b:?}) -- {message}"))
    }
}

/// Return a formatted error message if `a > b` is not true.
pub fn result_assert_gt<T>(a: T, b: T, message: &str) -> Result<(), String>
where
    T: std::fmt::Debug + std::cmp::PartialOrd,
{
    if a > b {
        Ok(())
    } else {
        Err(format!("!({a:?} > {b:?}) -- {message}"))
    }
}

/// Run the function and then close any given file descriptors, even if there was an error.
pub fn run_and_close_fds<'a, I, F, U>(fds: I, f: F) -> U
where
    I: IntoIterator<Item = &'a libc::c_int>,
    F: FnOnce() -> U,
{
    let rv = f();

    for fd in fds.into_iter() {
        let rv_close = unsafe { libc::close(*fd) };
        assert_eq!(rv_close, 0, "Could not close fd {fd}");
    }

    rv
}

/// Get the current errno.
pub fn get_errno() -> i32 {
    std::io::Error::last_os_error().raw_os_error().unwrap()
}

/// Get the message for the given errno.
pub fn get_errno_message(errno: i32) -> String {
    let cstr;
    unsafe {
        let error_ptr = libc::strerror(errno);
        cstr = std::ffi::CStr::from_ptr(error_ptr)
    }
    cstr.to_string_lossy().into_owned()
}

/// Assert the boolean condition is true, else print the last system error
pub fn assert_true_else_errno(cond: bool) {
    assert!(cond, "{}", get_errno_message(get_errno()));
}

/// Like the function `assert_true_else_errno`, but shows the original expression in the output
/// message.
#[macro_export]
macro_rules! assert_with_errno {
    ($f: expr) => {{
        let result = $f;
        let errno = test_utils::get_errno();
        let errno_str =
            linux_api::errno::Errno::from_u16(errno as u16).expect("errno is not valid");
        assert!(
            result,
            "assertion failed: {} (errno: {})",
            stringify!($f),
            errno_str
        );
        errno
    }};
}

/// Calls check_system_call(), but automatically passes the current line number.
#[macro_export]
macro_rules! check_system_call {
    ($f: expr, $expected_errnos: expr $(,)?) => {
        test_utils::check_system_call($f, $expected_errnos, line!());
    };
}

/// Run the given function, check that the errno was expected, and return the function's return value.
pub fn check_system_call<F, J>(
    f: F,
    expected_errnos: &[libc::c_int],
    line: u32,
) -> Result<J, String>
where
    F: FnOnce() -> J,
    J: std::cmp::Ord + std::convert::From<i8> + Copy + std::fmt::Display,
{
    let rv = f();
    let errno = get_errno();

    if expected_errnos.is_empty() {
        // if no error is expected (rv should be >= 0)
        if rv < 0.into() {
            return Err(format!(
                "Expecting a non-negative return value, received {} \"{}\" [line {}]",
                rv,
                get_errno_message(errno),
                line,
            ));
        }
    } else {
        // if we expect the system call to return an error (rv should be -1)
        if rv != (-1).into() {
            return Err(format!(
                "Expecting a return value of -1, received {rv} [line {line}]",
            ));
        }
        if !expected_errnos.contains(&errno) {
            return Err(format!(
                "Expecting errnos {:?}, received {} \"{}\" [line {}]",
                expected_errnos,
                errno,
                get_errno_message(errno),
                line,
            ));
        }
    }

    Ok(rv)
}

/// Similar to the `vec!` macro, `set!` will create a `HashSet` with the given elements.
///
/// ```
/// # use test_utils::*;
/// let s = set![1, 2, 3, 1];
/// assert_eq!(s.len(), 3);
/// ```
#[macro_export]
macro_rules! set {
    () => (
        std::collections::HashSet::new()
    );
    ($($x:expr),+ $(,)?) => (
        ([$($x),+]).iter().cloned().collect::<std::collections::HashSet<_>>()
    );
}

pub fn running_in_shadow() -> bool {
    // There is the same function in the C tests common code
    let Ok(ld_preload) = std::env::var("LD_PRELOAD") else {
        return false;
    };
    ld_preload.contains("libshadow_injector.so")
}

/// Returns `true` if the `POLLIN` flag is set.
pub fn is_readable(fd: libc::c_int, timeout_ms: i32) -> nix::Result<bool> {
    let mut poll_fds = [nix::poll::PollFd::new(fd, PollFlags::POLLIN)];
    let count = nix::poll::poll(&mut poll_fds, timeout_ms)?;

    Ok(count > 0 && poll_fds[0].revents().unwrap().contains(PollFlags::POLLIN))
}

/// Returns `true` if the `POLLOUT` flag is set.
pub fn is_writable(fd: libc::c_int, timeout_ms: i32) -> nix::Result<bool> {
    let mut poll_fds = [nix::poll::PollFd::new(fd, PollFlags::POLLOUT)];
    let count = nix::poll::poll(&mut poll_fds, timeout_ms)?;

    Ok(count > 0 && poll_fds[0].revents().unwrap().contains(PollFlags::POLLOUT))
}

/// Returns the poll event flags (the result of `poll()` with `PollFlags::all()` flags set). The
/// flags will be empty if the timeout occurred.
pub fn poll_status(fd: libc::c_int, timeout_ms: i32) -> nix::Result<PollFlags> {
    let mut poll_fds = [nix::poll::PollFd::new(fd, PollFlags::all())];
    let _count = nix::poll::poll(&mut poll_fds, timeout_ms)?;

    Ok(poll_fds[0].revents().unwrap_or(PollFlags::empty()))
}

pub struct Interruptor {
    cancellation_sender: mpsc::Sender<()>,
    handle: Option<thread::JoinHandle<()>>,
}

impl Interruptor {
    /// Creates an Interruptor that will send `signo` to the current thread
    /// after `t` has elapsed (unless cancelled in the meantime).
    pub fn new(t: Duration, signal: signal::Signal) -> Self {
        let (sender, receiver) = mpsc::channel();
        let tid = nix::unistd::gettid();

        let handle = thread::spawn(move || {
            if receiver.recv_timeout(t).is_ok() {
                // Cancelled
                return;
            } // else Timed out.
            unsafe { libc::syscall(libc::SYS_tkill, tid.as_raw(), signal as i32) };
        });

        Self {
            cancellation_sender: sender,
            handle: Some(handle),
        }
    }

    /// Cancel the interruption.
    pub fn cancel(&mut self) {
        if let Some(handle) = self.handle.take() {
            // Send a cancellation message. Ignore failure,
            // which will happen if the thread has already exited,
            // closing the receiver side of the channel.
            self.cancellation_sender.send(()).ok();
            handle.join().unwrap();
        }
    }
}

impl Drop for Interruptor {
    fn drop(&mut self) {
        self.cancel();
    }
}

extern "C" fn nop_handler(_signo: i32) {}

pub fn nop_sig_handler() -> nix::sys::signal::SigHandler {
    nix::sys::signal::SigHandler::Handler(nop_handler)
}

pub fn install_nop_signal_handler(signal: signal::Signal) -> anyhow::Result<()> {
    unsafe {
        nix::sys::signal::sigaction(
            signal,
            &nix::sys::signal::SigAction::new(
                nop_sig_handler(),
                nix::sys::signal::SaFlags::empty(),
                nix::sys::signal::SigSet::empty(),
            ),
        )?
    };
    Ok(())
}

/// Run a function that will interrupted with `SIGUSR1` after the given timeout.
pub fn interrupt_fn_exec<F>(interrupt_timeout: Duration, f: F) -> anyhow::Result<()>
where
    F: FnOnce() -> anyhow::Result<()>,
{
    let signo = signal::Signal::SIGUSR1;
    install_nop_signal_handler(signo)?;

    // Start a thread that will interrupt us after the timeout.
    let interruptor = Interruptor::new(interrupt_timeout, signo);

    // Run the function, which may be interrupted.
    f()?;

    // Cancel the interruptor, in case it hasn't already fired.
    drop(interruptor);
    Ok(())
}

/// Convenience wrapper around `anyhow::ensure` that generates useful error messages.
///
/// Example:
///
/// ```
/// # use test_utils::*;
/// # use anyhow::anyhow;
/// fn fn1() -> Result<(), anyhow::Error> {
///     let x = 2;
///     let y = 3;
///     ensure_ord!(x, >, y);
///     Ok(())
/// }
///
/// fn fn2() -> Result<(), anyhow::Error> {
///     return Err(anyhow!("!(2 > 3)"));
/// }
///
/// assert_eq!(format!("{}", fn1().unwrap_err()),
///            format!("{}", fn2().unwrap_err()));
/// ```
#[macro_export]
macro_rules! ensure_ord {
    ($lhs:expr, $ord:tt, $rhs:expr) => {
        let eval_lhs = $lhs;
        let eval_rhs = $rhs;
        anyhow::ensure!(eval_lhs $ord eval_rhs, "!({:?} {} {:?})", eval_lhs, stringify!($ord), eval_rhs);
    };
}

/// Convert a `&[u8]` to `&[i8]`. Useful when making C syscalls.
pub fn u8_to_i8_slice(s: &[u8]) -> &[i8] {
    // assume that if try_from() was successful, then a direct cast would also be
    assert!(s.iter().all(|x| i8::try_from(*x).is_ok()));
    unsafe { std::slice::from_raw_parts(s.as_ptr() as *const i8, s.len()) }
}

/// Convert a `&[i8]` to `&[u8]`. Useful when making C syscalls.
pub fn i8_to_u8_slice(s: &[i8]) -> &[u8] {
    // assume that if try_from() was successful, then a direct cast would also be
    assert!(s.iter().all(|x| u8::try_from(*x).is_ok()));
    unsafe { std::slice::from_raw_parts(s.as_ptr() as *const u8, s.len()) }
}

#[derive(Copy, Clone, Eq, PartialEq, Debug)]
pub struct ITimer {
    pub interval: TimeVal,
    pub value: TimeVal,
}

impl From<libc::itimerval> for ITimer {
    fn from(val: libc::itimerval) -> Self {
        Self {
            interval: TimeVal::from(val.it_interval),
            value: TimeVal::from(val.it_value),
        }
    }
}

// Neither `libc` nor `nix` wrap `getitimer`.
pub fn getitimer(which: i32) -> nix::Result<ITimer> {
    let mut old_value: libc::itimerval = unsafe { std::mem::zeroed() };
    if unsafe {
        libc::syscall(
            libc::SYS_getitimer,
            which,
            std::ptr::from_mut(&mut old_value),
        )
    } == -1
    {
        return Err(nix::errno::Errno::last());
    }
    Ok(old_value.into())
}

// Neither `libc` nor `nix` wrap `setitimer`.
pub fn setitimer(which: i32, new_value: &libc::itimerval) -> nix::Result<ITimer> {
    let mut old_value: libc::itimerval = unsafe { std::mem::zeroed() };
    if unsafe { libc::syscall(libc::SYS_setitimer, which, new_value, &mut old_value) } == -1 {
        return Err(nix::errno::Errno::last());
    }
    Ok(old_value.into())
}

/// Convert an iterator of slices to a [`Vec`] of [`IoSlice`](std::io::IoSlice) (which "is
/// guaranteed to be ABI compatible with the `iovec` type on Unix platforms").
pub fn iov_helper<'a, I, T>(iov: I) -> Vec<std::io::IoSlice<'a>>
where
    I: IntoIterator<Item = &'a T>,
    T: AsRef<[u8]> + 'a + ?Sized,
{
    iov.into_iter()
        .map(|x| std::io::IoSlice::new(x.as_ref()))
        .collect()
}

/// Convert an iterator of mutable slices to a [`Vec`] of [`IoSliceMut`](std::io::IoSliceMut) (which
/// "is guaranteed to be ABI compatible with the `iovec` type on Unix platforms").
pub fn iov_helper_mut<'a, I, T>(iov: I) -> Vec<std::io::IoSliceMut<'a>>
where
    I: IntoIterator<Item = &'a mut T>,
    T: AsMut<[u8]> + 'a + ?Sized,
{
    iov.into_iter()
        .map(|x| std::io::IoSliceMut::new(x.as_mut()))
        .collect()
}

/// Encodes the order in which Linux checks the syscall args.
///
/// When fuzzing syscalls and passing invalid values for multiple syscall args, this ordering
/// enables us to determine which invalid arg's associated error code is expected to be returned.
#[derive(Debug, Copy, Clone, Eq, Ord, PartialEq, PartialOrd)]
pub enum FuzzOrder {
    First,
    Second,
    Third,
    Fourth,
    Fifth,
    Sixth,
}

/// Helps us fuzz syscalls by encoding syscall argument fuzz values and the syscall result we expect
/// from Linux.
#[derive(Debug, Copy, Clone)]
pub struct FuzzArg<T> {
    /// The fuzz value to pass as an argument to a syscall.
    pub value: T,
    /// The expected result for passing the arg value into the syscall. If we expect the value to
    /// produce an error, the expected rv and/or errno are encoded in `FuzzerError`.
    pub expected_result: FuzzResult,
}

impl<T> FuzzArg<T> {
    /// Create a new `FuzzArg` without manually specifying the `FuzzArg` struct.
    pub fn new(value: T, expected_result: FuzzResult) -> Self {
        FuzzArg::<T> {
            value,
            expected_result,
        }
    }
}

/// Encodes the expected result of a particular syscall arg.
pub type FuzzResult = Result<(), FuzzError>;

/// Encodes that a fuzz test is expected to produce a syscall error. When validating a result, the
/// syscall rv and errno are optionally verfied if provided.
#[derive(Debug, Copy, Clone, Eq, Ord, PartialEq, PartialOrd)]
pub struct FuzzError {
    /// Encodes the order in which Linux checks the syscall args.
    pub order: FuzzOrder,
    /// If `Some`, this return value is expected as a syscall result.
    pub rv: Option<libc::c_int>,
    /// If `Some`, this errno value is expected as a syscall result.
    pub errno: Option<libc::c_int>,
}

impl FuzzError {
    /// Encode that a new syscall error with priority `order` should have occurred, optionally
    /// causing return val `rv` and/or `errno` to be returned.
    pub fn new(order: FuzzOrder, rv: Option<libc::c_int>, errno: Option<libc::c_int>) -> Self {
        FuzzError { order, rv, errno }
    }
}

/// Returns `FuzzError` items for the results where we expect errors.
pub fn filter_discard_valid(results: &[FuzzResult]) -> Vec<&FuzzError> {
    results.iter().filter_map(|x| x.as_ref().err()).collect()
}

/// Check that the actual syscall retval and errno matches the expected results.
pub fn verify_syscall_result(
    expected_results: Vec<FuzzResult>,
    expected_success_rv: libc::c_int,
    actual_rv: libc::c_int,
    actual_errno: libc::c_int,
) -> anyhow::Result<()> {
    // We want to ensure we have the correct error for invalid values.
    let mut expected_errors = filter_discard_valid(&expected_results);

    // Check the error according to the ordering defined by the caller.
    expected_errors.sort();

    if let Some(error) = expected_errors.first() {
        // The caller encoded that this should have been error.
        if let Some(expected_rv) = error.rv {
            // The caller wants to validate the return value.
            ensure_ord!(expected_rv, ==, actual_rv);
        }
        if let Some(expected_errno) = error.errno {
            // The caller wants to validate the errno.
            ensure_ord!(expected_errno, ==, actual_errno);
        }
    } else {
        // The caller encoded that the syscall should have returned success.
        ensure_ord!(expected_success_rv, ==, actual_rv);
    }
    Ok(())
}

/// Run a function and check that it returns within an expected duration.
pub fn check_fn_exec_duration<F, E>(
    expected: Duration,
    tolerance: Duration,
    f: F,
) -> anyhow::Result<()>
where
    F: FnOnce() -> Result<(), E>,
    anyhow::Error: From<E>,
{
    let before = SystemTime::now();
    f()?;
    let after = SystemTime::now();

    let actual = after.duration_since(before)?;
    let diff = time::duration_abs_diff(expected, actual);
    ensure_ord!(diff, <=, tolerance);

    Ok(())
}

/// Wraps a writable `OwnedFd` to accept values of type `T`.
// TODO: It'd be nice to wrap a generic `Write` object instead, but this is
// tricky to do in a sound way when T may have undefined padding bytes.
pub struct TypedWriter<T> {
    fd: std::os::fd::OwnedFd,
    _t: PhantomData<T>,
}
impl<T> TypedWriter<T>
where
    T: shadow_pod::Pod,
{
    fn new(fd: std::os::fd::OwnedFd) -> Self {
        Self {
            fd,
            _t: PhantomData,
        }
    }

    /// Send `val`
    pub fn send(&mut self, val: &T) -> Result<(), std::io::Error> {
        let bytes = shadow_pod::as_u8_slice(val);
        let mut total_written = 0;
        while total_written != bytes.len() {
            let nwritten = unsafe {
                libc::write(
                    self.fd.as_raw_fd(),
                    bytes.as_ptr().add(total_written).cast(),
                    bytes.len() - total_written,
                )
            };
            if nwritten == -1 {
                return Err(std::io::Error::last_os_error());
            }
            total_written += usize::try_from(nwritten).unwrap();
        }
        Ok(())
    }
}

/// Wraps a `std::io::Read` to read values of type `T`.
///
/// Assumes that no more data will become available after EOF is reached.
pub struct TypedReader<R, T> {
    reader: R,
    _t: PhantomData<T>,
}

impl<R, T> TypedReader<R, T>
where
    R: std::io::Read,
    T: shadow_pod::Pod,
{
    pub fn new(reader: R) -> Self {
        Self {
            reader,
            _t: PhantomData,
        }
    }

    /// Receive a `T`, or `None` if EOF is reached without reading any bytes.
    ///
    /// If EOF is reached in the middle of a value, an error with
    /// ErrorKind::UnexpectedEof is returned.
    pub fn recv(&mut self) -> Result<Option<T>, std::io::Error> {
        self.reader.read_pod()
    }
}

/// Creates a pipe for transferring values of type `T`.
#[allow(clippy::type_complexity)]
pub fn typed_pipe<T>() -> Result<(TypedReader<PipeReader, T>, TypedWriter<T>), std::io::Error>
where
    T: shadow_pod::Pod,
{
    let (reader, writer) = std::io::pipe()?;
    Ok((TypedReader::new(reader), TypedWriter::new(writer)))
}

/// A forked child process that runs some function in a loop.
///
/// The child process runs in a loop, accepting values of `RequestType`,
/// processing them with some given handler function to generate a
/// `ResponseType`, and sends them back.
///
/// The child exits when this object is dropped.
pub struct ForkedChild<RequestType, ResponseType> {
    pid: libc::c_int,
    cmd_writer: TypedWriter<RequestType>,
    res_reader: TypedReader<PipeReader, ResponseType>,
}

impl<RequestType, ResponseType> ForkedChild<RequestType, ResponseType>
where
    RequestType: shadow_pod::Pod,
    ResponseType: shadow_pod::Pod,
{
    /// Create a forked child process that used `f` as its handler function.
    pub fn new<FnType>(mut f: FnType) -> Result<Self, std::io::Error>
    where
        FnType: FnMut(&RequestType) -> ResponseType,
    {
        let (mut cmd_reader, cmd_writer) = typed_pipe::<RequestType>()?;
        let (res_reader, mut res_writer) = typed_pipe::<ResponseType>()?;
        let fork_res = unsafe { libc::fork() };
        let pid = match fork_res.cmp(&0) {
            std::cmp::Ordering::Equal => {
                // Child will run in a loop, processing requests.
                let child_closure = || {
                    // Close our cmd writer, to ensure we get EOF when the parent closes their copy,
                    // which happens implicitly when the ForkedChild is dropped.
                    drop(cmd_writer);
                    // Close our response reader while we're at it, though not strictly necessary.
                    drop(res_reader);
                    while let Some(cmd) = cmd_reader.recv().expect("Couldn't read from parent") {
                        let res = f(&cmd);
                        res_writer.send(&res).expect("Couldn't send to parent");
                    }
                };
                // Run the child closure while catching panics. This is to prevent returning
                // from this frame and circumventing the fast exit below.
                //
                // Normally the callable passed to `catch_unwind` must be
                // `UnwindSafe` to ensure that we won't see data with broken
                // invariants after a panic. It doesn't matter in this case
                // since we're exiting immediately afterwards, so we ignore this
                // requirement using the `AssertUnwindSafe` wrapper.
                let res = std::panic::catch_unwind(std::panic::AssertUnwindSafe(child_closure));
                // exit the child process. avoid returning or doing any normal
                // runtime teardown, which could have unintended consequences
                // due to resources being inherited from the parent.
                unsafe {
                    libc::_exit(if res.is_ok() { 0 } else { 1 });
                }
            }
            std::cmp::Ordering::Greater => fork_res,
            std::cmp::Ordering::Less => return Err(std::io::Error::last_os_error()),
        };
        Ok(Self {
            pid,
            cmd_writer,
            res_reader,
        })
    }

    /// Send a value to the child process.
    pub fn send(&mut self, req: &RequestType) -> Result<(), std::io::Error> {
        self.cmd_writer.send(req)
    }

    /// Receive a value from the child process.
    pub fn recv(&mut self) -> Result<ResponseType, std::io::Error> {
        let Some(res) = self.res_reader.recv()? else {
            // EOF from child is an error; child shouldn't close their end before we close ours.
            return Err(std::io::Error::new(
                std::io::ErrorKind::UnexpectedEof,
                "Child closed pipe",
            ));
        };
        Ok(res)
    }

    /// Send a value to the child process and get its response.
    pub fn send_recv(&mut self, req: &RequestType) -> Result<ResponseType, std::io::Error> {
        self.send(req)?;
        self.recv()
    }

    /// pid of the child process.
    pub fn pid(&self) -> i32 {
        self.pid
    }
}
