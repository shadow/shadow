/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

enum Cond<'a, T> {
    Any,
    Only(&'a [T]),
    Not(&'a [T]),
}

impl<T: std::cmp::Eq> Cond<'_, T> {
    fn matches(&self, compare: T) -> bool {
        match self {
            Cond::Only(vals) => vals.contains(&compare),
            Cond::Not(vals) => !vals.contains(&compare),
            Cond::Any => true,
        }
    }
}

struct ErrorCondition<'a> {
    domain: Cond<'a, libc::c_int>,
    sock_type: Cond<'a, libc::c_int>,
    flag: Cond<'a, libc::c_int>,
    protocol: Cond<'a, libc::c_int>,
    expected_errno: Option<libc::c_int>,
}

#[derive(Copy, Clone, PartialEq, Eq)]
struct SocketArguments {
    domain: libc::c_int,
    sock_type: libc::c_int,
    flag: libc::c_int,
    protocol: libc::c_int,
}

impl SocketArguments {
    fn matches(&self, compare: &ErrorCondition) -> bool {
        compare.domain.matches(self.domain)
            && compare.sock_type.matches(self.sock_type)
            && compare.flag.matches(self.flag)
            && compare.protocol.matches(self.protocol)
    }
}

impl std::fmt::Display for SocketArguments {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}, {}|{}, {}",
            self.domain, self.sock_type, self.flag, self.protocol
        )
    }
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum SocketFn {
    Socket,
    Syscall,
}

impl SocketFn {
    fn format(&self, args: &SocketArguments) -> String {
        match &self {
            SocketFn::Socket => format!("socket({args})"),
            SocketFn::Syscall => format!("syscall(SYS_socket, {args})"),
        }
    }
}

fn main() -> Result<(), String> {
    // should we run only tests that shadow supports
    let run_only_passing_tests = std::env::args().any(|x| x == "--shadow-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    // for combinations of the possible arguments, are there any that should return an error?
    let error_conditions = [
        // if we use an unsupported type
        ErrorCondition {
            domain: Cond::Any,
            sock_type: Cond::Not(&[
                libc::SOCK_STREAM,
                libc::SOCK_DGRAM,
                libc::SOCK_SEQPACKET,
                libc::SOCK_RAW,
                libc::SOCK_RDM,
            ]),
            flag: Cond::Any,
            protocol: Cond::Any,
            expected_errno: Some(libc::EINVAL),
        },
        // if we use an unsupported flag
        ErrorCondition {
            domain: Cond::Any,
            sock_type: Cond::Any,
            flag: Cond::Not(&[0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC]),
            protocol: Cond::Any,
            expected_errno: Some(libc::EINVAL),
        },
        // if we use an unsupported domain
        ErrorCondition {
            domain: Cond::Not(&[
                libc::AF_UNIX,
                libc::AF_LOCAL,
                libc::AF_INET,
                libc::AF_AX25,
                libc::AF_IPX,
                libc::AF_APPLETALK,
                libc::AF_X25,
                libc::AF_INET6,
                libc::AF_DECnet,
                libc::AF_KEY,
                libc::AF_NETLINK,
                libc::AF_PACKET,
                libc::AF_RDS,
                libc::AF_PPPOX,
                libc::AF_LLC,
                libc::AF_IB,
                libc::AF_MPLS,
                libc::AF_CAN,
                libc::AF_TIPC,
                libc::AF_BLUETOOTH,
                libc::AF_ALG,
                libc::AF_VSOCK,
                libc::AF_XDP,
            ]),
            sock_type: Cond::Any,
            flag: Cond::Any,
            protocol: Cond::Any,
            expected_errno: Some(libc::EAFNOSUPPORT),
        },
        // if we use the AF_AX25 domain without the SOCK_SEQPACKET type
        ErrorCondition {
            domain: Cond::Only(&[libc::AF_AX25]),
            sock_type: Cond::Not(&[libc::SOCK_SEQPACKET]),
            flag: Cond::Any,
            protocol: Cond::Any,
            expected_errno: Some(libc::EAFNOSUPPORT),
        },
        // if we use the SOCK_SEQPACKET type without the AF_UNIX domain
        ErrorCondition {
            domain: Cond::Not(&[libc::AF_UNIX, libc::AF_LOCAL]),
            sock_type: Cond::Only(&[libc::SOCK_SEQPACKET]),
            flag: Cond::Any,
            protocol: Cond::Any,
            expected_errno: Some(libc::ESOCKTNOSUPPORT),
        },
        // if we use the SOCK_RAW type without the AF_UNIX domain
        ErrorCondition {
            domain: Cond::Not(&[libc::AF_UNIX, libc::AF_LOCAL]),
            sock_type: Cond::Only(&[libc::SOCK_RAW]),
            flag: Cond::Any,
            protocol: Cond::Only(&[0]),
            expected_errno: Some(libc::EPROTONOSUPPORT),
        },
        // if we use the SOCK_RAW type without the AF_UNIX domain
        ErrorCondition {
            domain: Cond::Not(&[libc::AF_UNIX, libc::AF_LOCAL]),
            sock_type: Cond::Only(&[libc::SOCK_RAW]),
            flag: Cond::Any,
            protocol: Cond::Any,
            expected_errno: Some(libc::EPERM), // assuming we don't have the privileges
        },
        // if we use the TCP protocol without AF_INET{,6}
        ErrorCondition {
            domain: Cond::Not(&[libc::AF_INET, libc::AF_INET6]),
            sock_type: Cond::Any,
            flag: Cond::Any,
            protocol: Cond::Only(&[libc::IPPROTO_TCP]),
            expected_errno: Some(libc::EPROTONOSUPPORT),
        },
        // if we use the UDP protocol without AF_INET{,6}
        ErrorCondition {
            domain: Cond::Not(&[libc::AF_INET, libc::AF_INET6]),
            sock_type: Cond::Any,
            flag: Cond::Any,
            protocol: Cond::Only(&[libc::IPPROTO_UDP]),
            expected_errno: Some(libc::EPROTONOSUPPORT),
        },
        // if we use the SOCK_RDM type
        ErrorCondition {
            domain: Cond::Any,
            sock_type: Cond::Only(&[libc::SOCK_RDM]),
            flag: Cond::Any,
            protocol: Cond::Any,
            expected_errno: Some(libc::ESOCKTNOSUPPORT),
        },
        // if we use the TCP protocol without the SOCK_STREAM type
        ErrorCondition {
            domain: Cond::Any,
            sock_type: Cond::Not(&[libc::SOCK_STREAM]),
            flag: Cond::Any,
            protocol: Cond::Only(&[libc::IPPROTO_TCP]),
            expected_errno: Some(libc::EPROTONOSUPPORT),
        },
        // if we use the UDP protocol without the SOCK_DGRAM type
        ErrorCondition {
            domain: Cond::Any,
            sock_type: Cond::Not(&[libc::SOCK_DGRAM]),
            flag: Cond::Any,
            protocol: Cond::Only(&[libc::IPPROTO_UDP]),
            expected_errno: Some(libc::EPROTONOSUPPORT),
        },
    ];

    let tests = if run_only_passing_tests {
        get_passing_tests()
    } else {
        get_all_tests()
    };

    run_tests(tests.iter(), &error_conditions, summarize)?;

    println!("Success.");
    Ok(())
}

fn get_passing_tests() -> Vec<(SocketFn, SocketArguments)> {
    // the different arguments to try (including invalid args)
    let domains = [libc::AF_INET, libc::AF_UNIX, 0xABBA];
    let sock_types = [libc::SOCK_STREAM, libc::SOCK_DGRAM, libc::SOCK_SEQPACKET];
    let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC];
    let protocols = [0, libc::IPPROTO_TCP, libc::IPPROTO_UDP];

    // product of the sets of arguments
    let mut tests = Vec::new();

    for socket_fn in [SocketFn::Socket, SocketFn::Syscall].iter() {
        for domain in domains.iter() {
            for sock_type in sock_types.iter() {
                for flag in flags.iter() {
                    for protocol in protocols.iter() {
                        let args = SocketArguments {
                            domain: *domain,
                            sock_type: *sock_type,
                            flag: *flag,
                            protocol: *protocol,
                        };
                        tests.push((*socket_fn, args));
                    }
                }
            }
        }
    }

    tests
}

fn get_all_tests() -> Vec<(SocketFn, SocketArguments)> {
    // the full list of domains (including domains we don't tests) can be found at:
    // https://github.com/torvalds/linux/blob/master/include/linux/socket.h#L174
    let domains = [
        libc::AF_UNIX,
        libc::AF_LOCAL,
        libc::AF_INET,
        libc::AF_INET6,
        0xABBA,
    ];
    // since the behaviour when using SOCK_RAW depends on the user's privileges,
    // we don't test it
    let sock_types = [
        libc::SOCK_STREAM,
        libc::SOCK_DGRAM,
        libc::SOCK_SEQPACKET,
        //libc::SOCK_RAW,
        libc::SOCK_RDM,
        0xABBA,
    ];
    let flags = [0, libc::SOCK_NONBLOCK, libc::SOCK_CLOEXEC, 0xABBA];
    let protocols = [0, libc::IPPROTO_TCP, libc::IPPROTO_UDP];

    // product of the sets of arguments
    let mut tests = Vec::new();

    for socket_fn in [SocketFn::Socket, SocketFn::Syscall].iter() {
        for domain in domains.iter() {
            for sock_type in sock_types.iter() {
                for flag in flags.iter() {
                    for protocol in protocols.iter() {
                        let args = SocketArguments {
                            domain: *domain,
                            sock_type: *sock_type,
                            flag: *flag,
                            protocol: *protocol,
                        };
                        tests.push((*socket_fn, args));
                    }
                }
            }
        }
    }

    // add any shadow-passing tests we are missing
    for passing_test in get_passing_tests().iter() {
        if !tests.contains(passing_test) {
            tests.push(*passing_test);
        }
    }

    tests
}

fn run_tests<'a, I>(
    tests: I,
    error_conditions: &[ErrorCondition],
    summarize: bool,
) -> Result<(), String>
where
    I: Iterator<Item = &'a (SocketFn, SocketArguments)>,
{
    // the set of file descriptors returned
    let mut used_fds = std::collections::HashSet::new();

    // assume that we haven't closed 0, 1, or 2
    used_fds.insert(0);
    used_fds.insert(1);
    used_fds.insert(2);

    // number of tests that passed, and the total performed
    let mut num_passed = 0;
    let mut num_performed = 0;

    // run the tests
    for (socket_fn, args) in tests {
        // by default, we don't expect to fail or want to check the errno
        let mut should_error = false;
        let mut expected_errno = None;

        // check the failure conditions
        for cond in error_conditions.iter() {
            // if the arguments match one of the failure conditions
            if args.matches(cond) {
                should_error = true;
                expected_errno = cond.expected_errno;
                break;
            }
        }

        print!("Testing {}...", socket_fn.format(args));

        num_performed += 1;

        let fd;
        match check_socket_call(args, *socket_fn, should_error, expected_errno) {
            // the test passed
            Ok(result_fd) => {
                println!(" ✓");
                fd = result_fd;
                num_passed += 1;
            }
            // the test failed
            Err(err) => {
                println!(" ✗ ({err})");
                fd = None;
                // if not summarizing, should exit immediately
                if !summarize {
                    return Err("One of the tests failed.".to_string());
                }
            }
        }

        // check for duplicate fds
        if let Some(fd) = fd {
            assert!(!used_fds.contains(&fd), "Duplicate fd returned: {fd}");
            used_fds.insert(fd);
        }
    }

    println!("Passed {num_passed}/{num_performed}");

    if num_passed == num_performed {
        Ok(())
    } else {
        Err("One of the tests failed.".to_string())
    }
}

fn check_socket_call(
    args: &SocketArguments,
    socket_fn: SocketFn,
    should_error: bool,
    expected_errno: Option<libc::c_int>,
) -> Result<Option<libc::c_int>, String> {
    // run either socket() or syscall()
    let rv = match socket_fn {
        SocketFn::Socket => unsafe {
            libc::socket(args.domain, args.sock_type | args.flag, args.protocol)
        },
        SocketFn::Syscall => i32::try_from(unsafe {
            libc::syscall(
                libc::SYS_socket,
                args.domain,
                args.sock_type | args.flag,
                args.protocol,
            )
        })
        .unwrap(),
    };

    let errno = test_utils::get_errno();

    // if we expect the socket creation to return an error
    let fd = if should_error {
        if rv != -1 {
            return Err(format!("Expecting a return value of -1, received {rv}"));
        }

        None
    } else {
        if rv < 0 {
            return Err(format!(
                "Expecting a non-negative return value, received {rv}",
            ));
        }

        Some(rv)
    };

    // check the errno if we were given one
    if let Some(expected_errno) = expected_errno {
        if errno != expected_errno {
            return Err(format!(
                "Expecting errno {} \"{}\", received {} \"{}\"",
                expected_errno,
                test_utils::get_errno_message(expected_errno),
                errno,
                test_utils::get_errno_message(errno)
            ));
        }
    }

    Ok(fd)
}
