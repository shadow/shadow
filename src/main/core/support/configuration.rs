use std::ffi::{CStr, CString, OsStr, OsString};
use std::os::unix::ffi::OsStrExt;

use crate::cshadow as c;

#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq)]
#[repr(C)]
pub enum InterposeMethod {
    /// Attach to child using ptrace and use it to interpose syscalls etc.
    Ptrace,
    /// Use LD_PRELOAD to load a library that implements the libC interface which will
    /// route syscalls to Shadow.
    Preload,
    /// Use both PRELOAD and PTRACE based interposition.
    Hybrid,
}

impl std::str::FromStr for InterposeMethod {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "ptrace" => Ok(Self::Ptrace),
            "preload" => Ok(Self::Preload),
            "hybrid" => Ok(Self::Hybrid),
            _ => Err(format!("'{}' is not a valid interpose method", s)),
        }
    }
}

#[derive(Debug, Clone, Copy, Hash, PartialEq, Eq)]
#[repr(C)]
pub enum QDiscMode {
    Fifo,
    RoundRobin,
}

impl std::str::FromStr for QDiscMode {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "fifo" => Ok(Self::Fifo),
            "round-robin" => Ok(Self::RoundRobin),
            _ => Err(format!("'{}' is not a valid qdisc mode", s)),
        }
    }
}

/// Parses a string as a list of arguments following the shell's parsing rules. This
/// uses `g_shell_parse_argv()` for parsing.
#[allow(dead_code)]
fn parse_string_as_args(args_str: &OsStr) -> Result<Vec<OsString>, String> {
    if args_str.len() == 0 {
        return Ok(Vec::new());
    }

    let args_str = CString::new(args_str.as_bytes()).unwrap();

    // parse the argument string
    let mut argc: libc::c_int = 0;
    let mut argv: *mut *mut libc::c_char = std::ptr::null_mut();
    let mut error: *mut libc::c_char = std::ptr::null_mut();
    let rv = unsafe { c::process_parseArgStr(args_str.as_ptr(), &mut argc, &mut argv, &mut error) };

    // if there was an error, return a copy of the error string
    if !rv {
        let error_message = match error.is_null() {
            false => unsafe { CStr::from_ptr(error) }.to_str().unwrap(),
            true => "Unknown parsing error",
        }
        .to_string();

        unsafe { c::process_parseArgStrFree(argv, error) };
        return Err(error_message);
    }

    assert!(!argv.is_null());

    // copy the arg strings
    let args: Vec<_> = (0..argc)
        .map(|x| unsafe {
            let arg_ptr = *argv.add(x as usize);
            assert!(!arg_ptr.is_null());
            OsStr::from_bytes(CStr::from_ptr(arg_ptr).to_bytes()).to_os_string()
        })
        .collect();

    unsafe { c::process_parseArgStrFree(argv, error) };
    Ok(args)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_args() {
        let arg_str = r#"the quick brown fox "jumped over" the "\"lazy\" dog""#;
        let expected_args = &[
            "the",
            "quick",
            "brown",
            "fox",
            "jumped over",
            "the",
            "\"lazy\" dog",
        ];

        let arg_str: OsString = arg_str.into();
        let args = parse_string_as_args(&arg_str).unwrap();

        assert_eq!(args, expected_args);
    }

    #[test]
    fn test_parse_args_empty() {
        let arg_str = "";
        let expected_args: &[&str] = &[];

        let arg_str: OsString = arg_str.into();
        let args = parse_string_as_args(&arg_str).unwrap();

        assert_eq!(args, expected_args);
    }

    #[test]
    fn test_parse_args_error() {
        let arg_str = r#"hello "world"#;

        let arg_str: OsString = arg_str.into();
        let err_str = parse_string_as_args(&arg_str).unwrap_err();

        assert!(err_str.len() != 0);
    }
}
