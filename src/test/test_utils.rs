/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

//! Utilities helpful for writing Rust integration tests.

use std::collections::HashSet;
use std::fmt;

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
pub fn run_tests<'a, I, T: 'a, E: 'a>(tests: I, summarize: bool) -> Result<Vec<T>, E>
where
    I: IntoIterator<Item = &'a ShadowTest<T, E>>,
    E: std::fmt::Debug + std::fmt::Display,
{
    let mut results = vec![];

    for test in tests {
        print!("Testing {}...", test.name());

        match test.run() {
            Err(failure) => {
                println!(" ✗ ({})", failure);
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
            Some(ref v) => v as *const T,
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
            Some(ref mut v) => v as *mut T,
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
        Err(format!("{:?} != {:?} -- {}", a, b, message))
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
        Err(format!("{:?} == {:?} -- {}", a, b, message))
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
        assert_eq!(rv_close, 0, "Could not close fd {}", fd);
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
                "Expecting a return value of -1, received {} [line {}]",
                rv, line
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
    match std::env::var("SHADOW_SPAWNED") {
        Ok(val) => val != "",
        _ => false,
    }
}
