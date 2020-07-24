/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

struct GetsocknameArguments {
    fd: libc::c_int,
    addr: Option<libc::sockaddr_in>, // if None, a null pointer should be used
    addr_len: Option<libc::socklen_t>, // if None, a null pointer should be used
}

/// A boxed function to run as a test.
type TestFn = Box<dyn Fn() -> Result<(), String>>;

/// AsPtr and AsMutPtr traits inspired by https://stackoverflow.com/q/35885670
trait AsPtr<T> {
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

trait AsMutPtr<T> {
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

fn main() -> Result<(), String> {
    // should we run only tests that shadow supports
    let run_only_passing_tests = std::env::args().any(|x| x == "--shadow-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let tests = if run_only_passing_tests {
        get_passing_tests()
    } else {
        get_all_tests()
    };

    run_tests(tests.iter(), summarize)?;

    println!("Success.");
    Ok(())
}

fn get_passing_tests() -> std::collections::BTreeMap<String, TestFn> {
    #[rustfmt::skip]
    let tests: Vec<(String, TestFn)> = vec![
        ("test_invalid_fd".to_string(),
            Box::new(test_invalid_fd)),
        ("test_non_existent_fd".to_string(),
            Box::new(test_non_existent_fd)),
        ("test_null_addr".to_string(),
            Box::new(test_null_addr)),
        ("test_null_len".to_string(),
            Box::new(test_null_len)),
        ("test_short_len".to_string(),
            Box::new(test_short_len)),
        ("test_zero_len".to_string(),
            Box::new(test_zero_len)),
        ("test_unbound_socket".to_string(),
            Box::new(test_unbound_socket)),
        ("test_bound_socket".to_string(),
            Box::new(test_bound_socket)),
        ("test_dgram_socket".to_string(),
            Box::new(test_dgram_socket)),
        ("test_after_close".to_string(),
            Box::new(test_after_close)),
        ("test_connected_socket".to_string(),
            Box::new(test_connected_socket)),
    ];

    let num_tests = tests.len();
    let tests: std::collections::BTreeMap<_, _> = tests.into_iter().collect();

    // make sure we didn't have any duplicate tests
    assert_eq!(num_tests, tests.len());

    tests
}

fn get_all_tests() -> std::collections::BTreeMap<String, TestFn> {
    #[rustfmt::skip]
    let tests: Vec<(String, TestFn)> = vec![
        ("test_non_socket_fd".to_string(),
            Box::new(test_non_socket_fd)),
    ];

    let num_tests = tests.len();
    let mut tests: std::collections::BTreeMap<_, _> = tests.into_iter().collect();

    // make sure we didn't have any duplicate tests
    assert_eq!(num_tests, tests.len());

    // add all of the passing tests
    tests.extend(get_passing_tests());

    tests
}

fn run_tests<'a, I>(tests: I, summarize: bool) -> Result<(), String>
where
    I: Iterator<Item = (&'a String, &'a TestFn)>,
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

/// Test getsockname using an argument that cannot be a fd.
fn test_invalid_fd() -> Result<(), String> {
    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: -1,
        addr: None,
        addr_len: Some(5),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using an argument that could be a fd, but is not.
fn test_non_existent_fd() -> Result<(), String> {
    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: 8934,
        addr: None,
        addr_len: Some(5),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using a valid fd that is not a socket.
fn test_non_socket_fd() -> Result<(), String> {
    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: None,
        addr_len: Some(5),
    };

    check_getsockname_call(&mut args, Some(libc::ENOTSOCK))
}

/// Test getsockname using a valid fd, but with a NULL address.
fn test_null_addr() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: None,
        addr_len: Some(5),
    };

    run_and_close_fds(&[fd], || {
        check_getsockname_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getsockname using a valid fd and address, a NULL address length.
fn test_null_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || {
        check_getsockname_call(&mut args, Some(libc::EFAULT))
    })
}

/// Test getsockname using a valid fd and address, but an address length that is too small.
fn test_short_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 0u32.to_be(),
        },
        // since our buffer will be short by one byte, we will only be missing one byte of sin_zero
        sin_zero: [0, 0, 0, 0, 0, 0, 0, 1],
    };

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some((std::mem::size_of_val(&addr) - 1) as u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using a valid fd and address, but an address length of 0.
fn test_zero_len() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // the sockaddr that we expect to have after calling getsockname();
    let expected_addr = addr;

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(0u32),
    };

    // if the buffer was too small, the returned data will be truncated but we won't get an error
    run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using an unbound socket.
fn test_unbound_socket() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 0u32.to_be(),
        },
        sin_zero: [0; 8],
    };

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using a socket bound to a port on loopback.
fn test_bound_socket() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind to the expected sockaddr
    let rv = unsafe {
        libc::bind(
            fd,
            &expected_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&expected_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname using an unbound datagram socket.
fn test_dgram_socket() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 0u32.to_be(),
        },
        sin_zero: [0; 8],
    };

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    run_and_close_fds(&[fd], || check_getsockname_call(&mut args, None))?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned address is expected
    sockaddr_check_equal(&args.addr.unwrap(), &expected_addr)
}

/// Test getsockname after closing the socket.
fn test_after_close() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    // the sockaddr that we expect to have after calling getsockname()
    let expected_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind to the expected sockaddr
    let rv = unsafe {
        libc::bind(
            fd,
            &expected_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&expected_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // close the socket
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    // fill the sockaddr with dummy data
    let addr = libc::sockaddr_in {
        sin_family: 123u16,
        sin_port: 456u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: 789u32.to_be(),
        },
        sin_zero: [1; 8],
    };

    // getsockname() may mutate addr and addr_len
    let mut args = GetsocknameArguments {
        fd: fd,
        addr: Some(addr),
        addr_len: Some(std::mem::size_of_val(&addr) as u32),
    };

    check_getsockname_call(&mut args, Some(libc::EBADF))
}

/// Test getsockname using a socket bound to a port on loopback.
fn test_connected_socket() -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind and listen on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::listen(fd_server, 100) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // client arguments for getsockname()
    let mut args_client = GetsocknameArguments {
        fd: fd_client,
        // fill the sockaddr with dummy data
        addr: Some(libc::sockaddr_in {
            sin_family: 123u16,
            sin_port: 456u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: 789u32.to_be(),
            },
            sin_zero: [1; 8],
        }),
        addr_len: Some(std::mem::size_of::<libc::sockaddr_in>() as u32),
    };

    // server arguments for getsockname()
    let mut args_server = GetsocknameArguments {
        fd: fd_server,
        // fill the sockaddr with dummy data
        addr: Some(libc::sockaddr_in {
            sin_family: 123u16,
            sin_port: 456u16.to_be(),
            sin_addr: libc::in_addr {
                s_addr: 789u32.to_be(),
            },
            sin_zero: [1; 8],
        }),
        addr_len: Some(std::mem::size_of::<libc::sockaddr_in>() as u32),
    };

    run_and_close_fds(&[fd_client, fd_server], || {
        check_getsockname_call(&mut args_client, None)?;
        check_getsockname_call(&mut args_server, None)
    })?;

    // check that the returned length is expected
    result_assert_eq(
        args_client.addr_len.unwrap() as usize,
        std::mem::size_of_val(&args_client.addr.unwrap()),
        "Unexpected addr length",
    )?;
    result_assert_eq(
        args_server.addr_len.unwrap() as usize,
        std::mem::size_of_val(&args_server.addr.unwrap()),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected (except the port which is not
    // deterministic)
    result_assert_eq(
        args_client.addr.unwrap().sin_family,
        libc::AF_INET as u16,
        "Unexpected family",
    )?;
    result_assert_eq(
        args_client.addr.unwrap().sin_addr.s_addr,
        libc::INADDR_LOOPBACK.to_be(),
        "Unexpected address",
    )?;
    result_assert_eq(
        args_client.addr.unwrap().sin_zero,
        [0; 8],
        "Unexpected padding",
    )?;

    // check that the returned server address is expected
    sockaddr_check_equal(&args_server.addr.unwrap(), &server_addr)
}

fn sockaddr_check_equal(a: &libc::sockaddr_in, b: &libc::sockaddr_in) -> Result<(), String> {
    result_assert_eq(a.sin_family, b.sin_family, "Unexpected family")?;
    result_assert_eq(a.sin_port, b.sin_port, "Unexpected port")?;
    result_assert_eq(a.sin_addr.s_addr, b.sin_addr.s_addr, "Unexpected address")?;
    result_assert_eq(a.sin_zero, b.sin_zero, "Unexpected padding")
}

/*
fn result_assert(cond: bool, message: &str) -> Result<(), String> {
    if !cond {
        Err(message.to_string())
    } else {
        Ok(())
    }
}
*/

fn result_assert_eq<T>(a: T, b: T, message: &str) -> Result<(), String>
where
    T: std::fmt::Debug + std::cmp::PartialEq,
{
    if a != b {
        Err(format!("{:?} != {:?} -- {}", a, b, message))
    } else {
        Ok(())
    }
}

/// Run the function and then close any given file descriptors, even if there was an error.
fn run_and_close_fds<F>(fds: &[libc::c_int], mut f: F) -> Result<(), String>
where
    F: FnMut() -> Result<(), String>,
{
    let rv = f();

    for fd in fds.iter() {
        let fd = *fd;
        let rv_close = unsafe { libc::close(fd) };
        assert_eq!(rv_close, 0, "Could not close the fd");
    }

    rv
}

fn get_errno() -> i32 {
    std::io::Error::last_os_error().raw_os_error().unwrap()
}

fn get_errno_message(errno: i32) -> String {
    let cstr;
    unsafe {
        let error_ptr = libc::strerror(errno);
        cstr = std::ffi::CStr::from_ptr(error_ptr)
    }
    cstr.to_string_lossy().into_owned()
}

fn check_getsockname_call(
    args: &mut GetsocknameArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() as usize <= std::mem::size_of_val(&args.addr.unwrap()));
    }

    // will modify args.addr and args.addr_len
    let rv = unsafe {
        libc::getsockname(
            args.fd,
            args.addr.as_mut_ptr() as *mut libc::sockaddr,
            args.addr_len.as_mut_ptr(),
        )
    };

    let errno = get_errno();

    match expected_errno {
        // if we expect the socket() call to return an error (rv should be -1)
        Some(expected_errno) => {
            if rv != -1 {
                return Err(format!("Expecting a return value of -1, received {}", rv));
            }
            if errno != expected_errno {
                return Err(format!(
                    "Expecting errno {} \"{}\", received {} \"{}\"",
                    expected_errno,
                    get_errno_message(expected_errno),
                    errno,
                    get_errno_message(errno)
                ));
            }
        }
        // if no error is expected (rv should be 0)
        None => {
            if rv != 0 {
                return Err(format!(
                    "Expecting a return value of 0, received {} \"{}\"",
                    rv,
                    get_errno_message(errno)
                ));
            }
        }
    }

    Ok(())
}
