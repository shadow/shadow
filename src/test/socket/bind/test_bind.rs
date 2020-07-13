/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// Docker does not support IPv6, so IPv6 is not currently used
#[allow(dead_code)]
enum LibcSockAddr {
    In(libc::sockaddr_in),
    In6(libc::sockaddr_in6),
}

struct BindArguments {
    fd: libc::c_int,
    addr: Option<LibcSockAddr>, // if None, a null pointer should be used
    addr_len: Option<libc::socklen_t>, // if None, the length should be derived from addr
}

// a boxed function to run as a test
type TestFn = Box<dyn Fn() -> Result<(), String>>;

fn main() {
    // should we run only tests that shadow supports
    let run_only_passing_tests = match std::env::args().position(|x| x == "--shadow-passing") {
        Some(_) => true,
        None => false,
    };
    // should we summarize the results rather than exit on a failed test
    let summarize = match std::env::args().position(|x| x == "--summarize") {
        Some(_) => true,
        None => false,
    };

    let tests = if run_only_passing_tests {
        get_passing_tests()
    } else {
        get_all_tests()
    };

    if let Err(_) = run_tests(tests.iter(), summarize) {
        println!("Failed.");
        std::process::exit(1);
    }

    println!("Success.");
}

fn get_passing_tests() -> std::collections::BTreeMap<String, TestFn> {
    #[rustfmt::skip]
    let mut tests: Vec<(String, TestFn)> = vec![
        ("test_invalid_fd".to_string(),
            Box::new(test_invalid_fd)),
        ("test_non_existent_fd".to_string(),
            Box::new(test_non_existent_fd)),
        ("test_short_addr".to_string(),
            Box::new(test_short_addr)),
    ];

    // tests to repeat for different socket options
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            #[rustfmt::skip]
            let more_tests: Vec<(String, TestFn)> = vec![
                (append_args("test_ipv4"),
                    Box::new(move || test_ipv4(sock_type, flag))),
                (append_args("test_loopback"),
                    Box::new(move || test_loopback(sock_type, flag))),
                (append_args("test_any_interface"),
                    Box::new(move || test_any_interface(sock_type, flag))),
                (append_args("test_double_bind_socket"),
                    Box::new(move || test_double_bind_socket(sock_type, flag))),
                (append_args("test_double_bind_address"),
                    Box::new(move || test_double_bind_address(sock_type, flag))),
                (append_args("test_double_bind_loopback_and_any"),
                    Box::new(move || test_double_bind_loopback_and_any(false, sock_type, flag))),
                (append_args("test_double_bind_loopback_and_any <reversed>"),
                    Box::new(move || test_double_bind_loopback_and_any(true, sock_type, flag))),
                (append_args("test_unspecified_port"),
                    Box::new(move || test_unspecified_port(sock_type, flag))),
            ];

            tests.extend(more_tests);
        }
    }

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
        ("test_null_addr".to_string(),
            Box::new(test_null_addr)),
    ];

    // Docker does not support IPv6, so the following test is disabled for now
    /*
    // tests to repeat for different socket options
    for &sock_type in [libc::SOCK_STREAM, libc::SOCK_DGRAM].iter() {
        for &flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            // add details to the test names to avoid duplicates
            let append_args = |s| format!("{} <type={},flag={}>", s, sock_type, flag);

            #[rustfmt::skip]
            let more_tests: Vec<(String, TestFn)> = vec![
                (append_args("test_ipv6"),
                    Box::new(move || test_ipv6(sock_type, flag)))
            ];

            tests.extend(more_tests);
        }
    }
    */

    let num_tests = tests.len();
    let mut tests: std::collections::BTreeMap<_, _> = tests.into_iter().collect();

    // make sure we didn't have any duplicate tests
    assert_eq!(num_tests, tests.len());

    // add all of the passing tests
    tests.extend(get_passing_tests());

    tests
}

fn run_tests<'a, I>(tests: I, summarize: bool) -> Result<(), ()>
where
    I: Iterator<Item = (&'a String, &'a TestFn)>,
{
    for (test_name, test_fn) in tests {
        print!("Testing {}...", test_name);

        match test_fn() {
            Err(msg) => {
                println!(" ✗ ({})", msg);
                if !summarize {
                    return Err(());
                }
            }
            Ok(_) => {
                println!(" ✓");
            }
        }
    }

    Ok(())
}

// test binding using an argument that cannot be a fd
fn test_invalid_fd() -> Result<(), String> {
    let args = BindArguments {
        fd: -1,
        addr: None,
        addr_len: Some(5),
    };

    check_bind_call(&args, Some(libc::EBADF))
}

// test binding using an argument that could be a fd, but is not
fn test_non_existent_fd() -> Result<(), String> {
    let args = BindArguments {
        fd: 8934,
        addr: None,
        addr_len: Some(5),
    };

    check_bind_call(&args, Some(libc::EBADF))
}

// test binding a valid fd that is not a socket
fn test_non_socket_fd() -> Result<(), String> {
    let args = BindArguments {
        fd: 0, // assume the fd 0 is already open and is not a socket
        addr: None,
        addr_len: Some(5),
    };

    check_bind_call(&args, Some(libc::ENOTSOCK))
}

// test binding a valid fd, but with a NULL address
fn test_null_addr() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let args = BindArguments {
        fd: fd,
        addr: None,
        addr_len: Some(5),
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EFAULT)))
}

// test binding a valid fd and address, but an address length that is too low
fn test_short_addr() -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: Some((std::mem::size_of::<libc::sockaddr_in>() - 1) as u32),
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, Some(libc::EINVAL)))
}

// test binding an INET socket
fn test_ipv4(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// Docker does not support IPv6, so this test is not run
#[allow(dead_code)]
// test binding an INET6 socket
fn test_ipv6(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET6, sock_type | flag, 0) };
    assert!(fd >= 0);

    let mut loopback = [0; 16];
    loopback[15] = 1;

    let addr = libc::sockaddr_in6 {
        sin6_family: libc::AF_INET6 as u16,
        sin6_port: 11111u16.to_be(),
        sin6_flowinfo: 0,
        sin6_addr: libc::in6_addr { s6_addr: loopback },
        sin6_scope_id: 0,
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In6(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding a socket on the loopback interface
fn test_loopback(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding a socket on all interfaces
fn test_any_interface(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// test binding a socket twice to the same address on the loopback interface
fn test_double_bind_socket(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || {
        check_bind_call(&args, None)?;
        check_bind_call(&args, Some(libc::EINVAL))?;
        Ok(())
    })
}

// test binding two sockets to the same address on the loopback interface
fn test_double_bind_address(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd1 = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd1 >= 0);
    let fd2 = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd2 >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args1 = BindArguments {
        fd: fd1,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    let args2 = BindArguments {
        fd: fd2,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd1, fd2], || {
        check_bind_call(&args1, None)?;
        check_bind_call(&args2, Some(libc::EADDRINUSE))?;
        Ok(())
    })
}

// test binding two sockets to the same address, but using both 'loopback' and 'any' interfaces
fn test_double_bind_loopback_and_any(
    reverse: bool,
    sock_type: libc::c_int,
    flag: libc::c_int,
) -> Result<(), String> {
    let fd1 = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd1 >= 0);
    let fd2 = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd2 >= 0);

    let addr1 = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    let addr2 = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 11111u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    // if reverse, bind to ANY before LOOPBACK
    let (addr1, addr2) = if reverse {
        (addr2, addr1)
    } else {
        (addr1, addr2)
    };

    let args1 = BindArguments {
        fd: fd1,
        addr: Some(LibcSockAddr::In(addr1)),
        addr_len: None,
    };

    let args2 = BindArguments {
        fd: fd2,
        addr: Some(LibcSockAddr::In(addr2)),
        addr_len: None,
    };

    run_and_close_fds(&[fd1, fd2], || {
        check_bind_call(&args1, None)?;
        check_bind_call(&args2, Some(libc::EADDRINUSE))?;
        Ok(())
    })
}

// test binding to all interfaces with a port of 0
fn test_unspecified_port(sock_type: libc::c_int, flag: libc::c_int) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, sock_type | flag, 0) };
    assert!(fd >= 0);

    let addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_ANY.to_be(),
        },
        sin_zero: [0; 8],
    };

    let args = BindArguments {
        fd: fd,
        addr: Some(LibcSockAddr::In(addr)),
        addr_len: None,
    };

    run_and_close_fds(&[fd], || check_bind_call(&args, None))
}

// run the function and then close any given file descriptors, even if there was an error
fn run_and_close_fds<F>(fds: &[libc::c_int], f: F) -> Result<(), String>
where
    F: Fn() -> Result<(), String>,
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

fn check_bind_call(
    args: &BindArguments,
    expected_errno: Option<libc::c_int>,
) -> Result<(), String> {
    // get a pointer to the sockaddr and the size of the structure
    // careful use of references here makes sure we don't copy memory, leading to stale pointers
    let (addr_ptr, real_addr_len) = match &args.addr {
        // get the tuple of (ptr, length)
        Some(addr) => match &addr {
            // libc::bind() requires the data to be cast to a libc::sockaddr pointer
            &LibcSockAddr::In(sockaddr_in) => (
                sockaddr_in as *const libc::sockaddr_in as *const libc::sockaddr,
                std::mem::size_of_val(sockaddr_in),
            ),
            &LibcSockAddr::In6(sockaddr_in6) => (
                sockaddr_in6 as *const libc::sockaddr_in6 as *const libc::sockaddr,
                std::mem::size_of_val(sockaddr_in6),
            ),
        },
        None => (std::ptr::null(), 0),
    };

    // if we were given a length, use that instead
    let addr_len = match args.addr_len {
        Some(len) => len,
        None => real_addr_len as u32,
    };

    // if the pointer is non-null, make sure the size is not greater than the actual data size so
    // that we don't segfault
    assert!(addr_ptr.is_null() || addr_len as usize <= real_addr_len);

    let rv = unsafe { libc::bind(args.fd, addr_ptr, addr_len) };

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
