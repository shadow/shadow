/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

struct AcceptArguments {
    fd: libc::c_int,
    addr: Option<libc::sockaddr_in>,
    addr_len: Option<libc::socklen_t>,
    flags: libc::c_int,
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
enum AcceptFn {
    Accept,
    Accept4,
}

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

/// A boxed function to run as a test.
type TestFn = Box<dyn Fn() -> Result<(), String>>;

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
    let mut tests: Vec<(String, TestFn)> = vec![];

    for &accept_fn in [AcceptFn::Accept, AcceptFn::Accept4].iter() {
        let append_args = |s| format!("{} <fn={:?}>", s, accept_fn);

        #[rustfmt::skip]
        let more_tests: Vec<(String, TestFn)> = vec![
            (append_args("test_invalid_fd"),
                Box::new(move || test_invalid_fd(accept_fn))),
            (append_args("test_non_existent_fd"),
                Box::new(move || test_non_existent_fd(accept_fn))),
            (append_args("test_invalid_sock_type"),
                Box::new(move || test_invalid_sock_type(accept_fn))),
        ];

        tests.extend(more_tests);

        let accept_flags = [
            0,
            libc::SOCK_NONBLOCK,
            libc::SOCK_CLOEXEC,
            libc::SOCK_NONBLOCK | libc::SOCK_CLOEXEC,
        ];

        for &sock_flag in [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC].iter() {
            for &accept_flag in accept_flags.iter() {
                // since accept() doesn't accept flags, we should skip them
                if accept_flag != 0 && accept_fn != AcceptFn::Accept4 {
                    continue;
                }

                let append_args = |s| {
                    format!(
                        "{} <fn={:?},sock_flag={},accept_flag={:?}>",
                        s, accept_fn, sock_flag, accept_flag
                    )
                };

                #[rustfmt::skip]
                let more_tests: Vec<(String, TestFn)> = vec![
                    (append_args("test_non_listening_fd"),
                        Box::new(move || test_non_listening_fd(accept_fn, sock_flag, accept_flag))),
                    (append_args("test_null_addr"),
                        Box::new(move || test_null_addr(accept_fn, sock_flag, accept_flag))),
                    (append_args("test_null_len"),
                        Box::new(move || test_null_len(accept_fn, sock_flag, accept_flag))),
                    (append_args("test_short_len"),
                        Box::new(move || test_short_len(accept_fn, sock_flag, accept_flag))),
                    (append_args("test_zero_len"),
                        Box::new(move || test_zero_len(accept_fn, sock_flag, accept_flag))),
                    (append_args("test_after_close"),
                        Box::new(move || test_after_close(accept_fn, sock_flag, accept_flag))),
                    (append_args("test_correctness <sleep=true>"),
                        Box::new(move || test_correctness(accept_fn, sock_flag, accept_flag, true))),
                ];

                tests.extend(more_tests);
            }
        }
    }

    let num_tests = tests.len();
    let tests: std::collections::BTreeMap<_, _> = tests.into_iter().collect();

    // make sure we didn't have any duplicate tests
    assert_eq!(num_tests, tests.len());

    tests
}

fn get_all_tests() -> std::collections::BTreeMap<String, TestFn> {
    let mut tests: Vec<(String, TestFn)> = vec![];

    for &accept_fn in [AcceptFn::Accept, AcceptFn::Accept4].iter() {
        let append_args = |s| format!("{} <fn={:?}>", s, accept_fn);

        #[rustfmt::skip]
        let more_tests: Vec<(String, TestFn)> = vec![
            (append_args("test_non_socket_fd"),
                Box::new(move || test_non_socket_fd(accept_fn))),
            // while running in shadow, you currently need a sleep before calling accept()
            // to allow shadow to process events (specifically the event for the SYN packet
            // from connect())
            (append_args("test_correctness <sleep=false>"),
                Box::new(move || test_correctness(accept_fn, 0, 0, false))),
        ];

        tests.extend(more_tests);
    }

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

/// Test accept using an argument that cannot be a fd.
fn test_invalid_fd(accept_fn: AcceptFn) -> Result<(), String> {
    let mut args = AcceptArguments {
        fd: -1,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::EBADF))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept using an argument that could be a fd, but is not.
fn test_non_existent_fd(accept_fn: AcceptFn) -> Result<(), String> {
    let mut args = AcceptArguments {
        fd: 8934,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::EBADF))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept using a valid fd that is not a socket.
fn test_non_socket_fd(accept_fn: AcceptFn) -> Result<(), String> {
    // assume the fd 0 is already open and is not a socket
    let mut args = AcceptArguments {
        fd: 0,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::ENOTSOCK))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept using an invalid socket type.
fn test_invalid_sock_type(accept_fn: AcceptFn) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_DGRAM, 0) };
    assert!(fd >= 0);

    let mut args = AcceptArguments {
        fd: fd,
        addr: None,
        addr_len: None,
        flags: 0,
    };

    run_and_close_fds(&[fd], || {
        let fd = check_accept_call(&mut args, accept_fn, Some(libc::EOPNOTSUPP))?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using a non-listening socket.
fn test_non_listening_fd(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd >= 0);

    let mut args = AcceptArguments {
        fd: fd,
        addr: None,
        addr_len: None,
        flags: accept_flag,
    };

    run_and_close_fds(&[fd], || {
        let fd = check_accept_call(&mut args, accept_fn, Some(libc::EINVAL))?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using a NULL pointer in the address argument.
fn test_null_addr(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // get the assigned port number
    let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
    let rv = unsafe {
        libc::getsockname(
            fd_server,
            &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
            &mut server_addr_size as *mut libc::socklen_t,
        )
    };
    assert_eq!(rv, 0);
    assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);

    // listen for connections
    let rv = unsafe { libc::listen(fd_server, 10) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert!(rv == 0 || (rv == -1 && get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(2000) };
    assert_eq!(rv, 0);

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: None,
        addr_len: Some(5),
        flags: accept_flag,
    };

    run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, None)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using a NULL pointer in the address length argument.
fn test_null_len(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // get the assigned port number
    let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
    let rv = unsafe {
        libc::getsockname(
            fd_server,
            &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
            &mut server_addr_size as *mut libc::socklen_t,
        )
    };
    assert_eq!(rv, 0);
    assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);

    // listen for connections
    let rv = unsafe { libc::listen(fd_server, 10) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    //assert_eq!(rv, 0, "{}", get_errno_message(get_errno()));
    assert!(rv == 0 || (rv == -1 && get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(2000) };
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

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: Some(addr),
        addr_len: None,
        flags: accept_flag,
    };

    run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, Some(libc::EFAULT))?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })
}

/// Test accept using an address length that is too small.
fn test_short_len(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // get the assigned port number
    let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
    let rv = unsafe {
        libc::getsockname(
            fd_server,
            &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
            &mut server_addr_size as *mut libc::socklen_t,
        )
    };
    assert_eq!(rv, 0);
    assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);

    // listen for connections
    let rv = unsafe { libc::listen(fd_server, 10) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    //assert_eq!(rv, 0, "{}", get_errno_message(get_errno()));
    assert!(rv == 0 || (rv == -1 && get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(10000) };
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

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: Some(addr),
        addr_len: Some((std::mem::size_of_val(&addr) - 1) as u32),
        flags: accept_flag,
    };

    run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, None)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected
    result_assert_eq(
        args.addr.unwrap().sin_family,
        libc::AF_INET as u16,
        "Unexpected family",
    )?;
    result_assert(
        args.addr.unwrap().sin_port != 0u16.to_be(),
        "Unexpected port",
    )?;
    result_assert_eq(
        args.addr.unwrap().sin_addr.s_addr,
        libc::INADDR_LOOPBACK.to_be(),
        "Unexpected address",
    )?;
    result_assert_eq(
        args.addr.unwrap().sin_zero,
        [0, 0, 0, 0, 0, 0, 0, 1],
        "Unexpected padding",
    )
}

/// Test accept using an address length of 0.
fn test_zero_len(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // get the assigned port number
    let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
    let rv = unsafe {
        libc::getsockname(
            fd_server,
            &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
            &mut server_addr_size as *mut libc::socklen_t,
        )
    };
    assert_eq!(rv, 0);
    assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);

    // listen for connections
    let rv = unsafe { libc::listen(fd_server, 10) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    //assert_eq!(rv, 0, "{}", get_errno_message(get_errno()));
    assert!(rv == 0 || (rv == -1 && get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    let rv = unsafe { libc::usleep(10000) };
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

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd_server,
        addr: Some(addr),
        addr_len: Some(0u32),
        flags: accept_flag,
    };

    run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, None)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&addr),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected
    result_assert_eq(args.addr.unwrap().sin_family, 123u16, "Unexpected family")?;
    result_assert_eq(
        args.addr.unwrap().sin_port,
        456u16.to_be(),
        "Unexpected port",
    )?;
    result_assert_eq(
        args.addr.unwrap().sin_addr.s_addr,
        789u32.to_be(),
        "Unexpected address",
    )?;
    result_assert_eq(args.addr.unwrap().sin_zero, [1; 8], "Unexpected padding")
}

/// Test accept after closing the socket.
fn test_after_close(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
) -> Result<(), String> {
    let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd >= 0);

    // the server address
    let server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    let rv = unsafe {
        libc::bind(
            fd,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // listen for connections
    let rv = unsafe { libc::listen(fd, 10) };
    assert_eq!(rv, 0);

    // close the connection
    let rv = unsafe { libc::close(fd) };
    assert_eq!(rv, 0);

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
        fd: fd,
        addr: None,
        addr_len: None,
        flags: accept_flag,
    };

    let fd = check_accept_call(&mut args, accept_fn, Some(libc::EBADF))?;
    if let Some(fd) = fd {
        let rv = unsafe { libc::close(fd) };
        assert_eq!(rv, 0, "Could not close the fd");
    }

    Ok(())
}

/// Test accept by checking the returned address fields.
fn test_correctness(
    accept_fn: AcceptFn,
    sock_flag: libc::c_int,
    accept_flag: libc::c_int,
    use_sleep: bool,
) -> Result<(), String> {
    let fd_client = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    let fd_server = unsafe { libc::socket(libc::AF_INET, libc::SOCK_STREAM | sock_flag, 0) };
    assert!(fd_client >= 0);
    assert!(fd_server >= 0);

    // the server address
    let mut server_addr = libc::sockaddr_in {
        sin_family: libc::AF_INET as u16,
        sin_port: 0u16.to_be(),
        sin_addr: libc::in_addr {
            s_addr: libc::INADDR_LOOPBACK.to_be(),
        },
        sin_zero: [0; 8],
    };

    // bind on the server address
    let rv = unsafe {
        libc::bind(
            fd_server,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert_eq!(rv, 0);

    // get the assigned port number
    let mut server_addr_size = std::mem::size_of_val(&server_addr) as u32;
    let rv = unsafe {
        libc::getsockname(
            fd_server,
            &mut server_addr as *mut libc::sockaddr_in as *mut libc::sockaddr,
            &mut server_addr_size as *mut libc::socklen_t,
        )
    };
    assert_eq!(rv, 0);
    assert_eq!(server_addr_size, std::mem::size_of_val(&server_addr) as u32);

    // listen for connections
    let rv = unsafe { libc::listen(fd_server, 10) };
    assert_eq!(rv, 0);

    // connect to the server address
    let rv = unsafe {
        libc::connect(
            fd_client,
            &server_addr as *const libc::sockaddr_in as *const libc::sockaddr,
            std::mem::size_of_val(&server_addr) as u32,
        )
    };
    assert!(rv == 0 || (rv == -1 && get_errno() == libc::EINPROGRESS));

    // shadow needs to run events, otherwise the accept call won't know it
    // has an incoming connection (SYN packet)
    if use_sleep {
        let rv = unsafe { libc::usleep(10000) };
        assert_eq!(rv, 0);
    }

    // accept() may mutate addr and addr_len
    let mut args = AcceptArguments {
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
        flags: accept_flag,
    };

    run_and_close_fds(&[fd_client, fd_server], || {
        let fd = check_accept_call(&mut args, accept_fn, None)?;
        if let Some(fd) = fd {
            let rv = unsafe { libc::close(fd) };
            assert_eq!(rv, 0, "Could not close the fd");
        }
        Ok(())
    })?;

    // check that the returned length is expected
    result_assert_eq(
        args.addr_len.unwrap() as usize,
        std::mem::size_of_val(&args.addr.unwrap()),
        "Unexpected addr length",
    )?;

    // check that the returned client address is expected (except the port which is not
    // deterministic)
    result_assert_eq(
        args.addr.unwrap().sin_family,
        libc::AF_INET as u16,
        "Unexpected family",
    )?;
    result_assert(
        args.addr.unwrap().sin_port != 0u16.to_be(),
        "Unexpected port",
    )?;
    result_assert_eq(
        args.addr.unwrap().sin_addr.s_addr,
        libc::INADDR_LOOPBACK.to_be(),
        "Unexpected address",
    )?;
    result_assert_eq(args.addr.unwrap().sin_zero, [0; 8], "Unexpected padding")
}

fn result_assert(cond: bool, message: &str) -> Result<(), String> {
    if !cond {
        Err(message.to_string())
    } else {
        Ok(())
    }
}

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

fn check_accept_call(
    args: &mut AcceptArguments,
    accept_fn: AcceptFn,
    expected_errno: Option<libc::c_int>,
) -> Result<Option<libc::c_int>, String> {
    // if the pointers will be non-null, make sure the length is not greater than the actual data size
    // so that we don't segfault
    if args.addr.is_some() && args.addr_len.is_some() {
        assert!(args.addr_len.unwrap() as usize <= std::mem::size_of_val(&args.addr.unwrap()));
    }

    let rv = match accept_fn {
        AcceptFn::Accept => unsafe {
            assert_eq!(args.flags, 0);
            libc::accept(
                args.fd,
                args.addr.as_mut_ptr() as *mut libc::sockaddr,
                args.addr_len.as_mut_ptr(),
            )
        },
        AcceptFn::Accept4 => unsafe {
            libc::accept4(
                args.fd,
                args.addr.as_mut_ptr() as *mut libc::sockaddr,
                args.addr_len.as_mut_ptr(),
                args.flags,
            )
        },
    };

    let errno = get_errno();
    let fd;

    match expected_errno {
        // if we expect the accept() call to return an error (rv should be -1)
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
            fd = None;
        }
        // if no error is expected (rv should be non-negative)
        None => {
            if rv < 0 {
                return Err(format!(
                    "Expecting a non-negative return value, received {} \"{}\"",
                    rv,
                    get_errno_message(errno)
                ));
            }
            fd = Some(rv);
        }
    }

    Ok(fd)
}
