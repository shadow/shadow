/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

//! Utilities helpful for writing Rust integration tests.

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

/// A boxed function to run as a test.
pub type TestFn = Box<dyn Fn() -> Result<(), String>>;

/// Runs provided named tests and outputs results to stdout.
pub fn run_tests<'a, I>(tests: I, summarize: bool) -> Result<(), String>
where
    I: IntoIterator<Item = (&'a String, &'a TestFn)>,
{
    for (test_name, test_fn) in tests {
        print!("Testing {}...", test_name);

        match test_fn() {
            Err(msg) => {
                println!(" ✗ ({})", msg);
                if !summarize {
                    return Err("One of the tests failed.".to_string());
                }
            }
            Ok(_) => {
                println!(" ✓");
            }
        }
    }

    Ok(())
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

/// Run the function and then close any given file descriptors, even if there was an error.
pub fn run_and_close_fds<'a, I, F>(fds: I, f: F) -> Result<(), String>
where
    I: IntoIterator<Item = &'a libc::c_int>,
    F: FnOnce() -> Result<(), String>,
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
