use std::os::fd::IntoRawFd;
use std::time::Duration;

use nix::errno::Errno;
use nix::sys::epoll::{self, EpollFlags};
use nix::unistd;

use test_utils::{ShadowTest, TestEnvironment, ensure_ord, set};

#[derive(Debug)]
struct WaiterResult {
    duration: Duration,
    epoll_res: nix::Result<usize>,
    events: Vec<epoll::EpollEvent>,
}

fn do_epoll_wait(epoll_fd: i32, timeout: Duration, do_read: bool) -> WaiterResult {
    let mut events = Vec::new();
    events.resize(10, epoll::EpollEvent::empty());

    let t0 = std::time::Instant::now();

    let res = epoll::epoll_wait(
        epoll_fd,
        &mut events,
        timeout.as_millis().try_into().unwrap(),
    );

    let t1 = std::time::Instant::now();

    events.resize(res.unwrap_or(0), epoll::EpollEvent::empty());

    if do_read {
        for ev in &events {
            let fd = ev.data() as i32;
            // we don't care if the read is successful or not (another thread may have already read)
            let _ = unistd::read(fd, &mut [0]);
        }
    }

    WaiterResult {
        duration: t1.duration_since(t0),
        epoll_res: res,
        events,
    }
}

fn test_threads_edge() -> anyhow::Result<()> {
    let (readfd, writefd) = unistd::pipe()?;
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        let timeout = Duration::from_millis(100);

        let threads = [
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
        ];

        // Wait for readers to block.
        std::thread::sleep(timeout / 2);

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        let mut results = threads.map(|t| t.join().unwrap());

        // One of the threads should have gotten an event, but we don't know which one.
        // Sort results by number of events received.
        results.sort_by(|lhs, rhs| lhs.events.len().cmp(&rhs.events.len()));

        // One thread should have timed out with no events received.
        ensure_ord!(results[0].epoll_res, ==, Ok(0));
        ensure_ord!(results[0].duration, >=, timeout);

        // The other should have gotten a single event.
        ensure_ord!(results[1].epoll_res, ==, Ok(1));
        ensure_ord!(results[1].duration, <, timeout);
        ensure_ord!(results[1].events[0], ==, epoll::EpollEvent::new(EpollFlags::EPOLLIN, 0));

        Ok(())
    })
}

#[derive(Copy, Clone, Debug)]
enum UseEPOLLET {
    Yes,
    No,
}

#[derive(Copy, Clone, Debug)]
enum UseEPOLLRDHUP {
    Yes,
    No,
}

#[derive(Copy, Clone, Debug)]
enum MakeReadable {
    Yes,
    No,
}

#[derive(Copy, Clone, Debug)]
enum FdType {
    Pipe,
    TcpStream,
}

// Test various combination of behavior when EOF is reached.
// Notably, the Rust async runtime tokio seems to rely on receiving EPOLLRDHUP for sockets.
fn test_threads_eof(
    use_edge: UseEPOLLET,
    use_rdhup: UseEPOLLRDHUP,
    make_readable: MakeReadable,
    fd_type: FdType,
) -> anyhow::Result<()> {
    let (readfd, writefd) = match fd_type {
        FdType::Pipe => {
            let (readfd, writefd) = unistd::pipe()?;
            (readfd, writefd)
        }
        FdType::TcpStream => {
            let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
            let client = std::net::TcpStream::connect(listener.local_addr().unwrap()).unwrap();
            let server = listener.accept().unwrap();
            (client.into_raw_fd(), server.0.into_raw_fd())
        }
    };

    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd], || {
        // We don't need to subscribe to EPOLLHUP; those events should
        // be generated regardless.
        // from epoll_ctl(2):
        // > epoll_wait(2) will always wait for this event; it is not necessary
        // > to set it in events when calling epoll_ctl().
        let mut events = EpollFlags::EPOLLIN;
        match use_rdhup {
            UseEPOLLRDHUP::Yes => events |= EpollFlags::EPOLLRDHUP,
            UseEPOLLRDHUP::No => (),
        };
        match use_edge {
            UseEPOLLET::Yes => events |= EpollFlags::EPOLLET,
            UseEPOLLET::No => (),
        };
        let mut event = epoll::EpollEvent::new(events, 0);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        // We do the close and (optional) write *before* calling epoll_wait to
        // be sure that the result has both events (when we do the optional
        // write).

        // Optionally make the read-end readable.
        match make_readable {
            MakeReadable::Yes => {
                unistd::write(writefd, &[0])?;
            }
            MakeReadable::No => {}
        }
        unistd::close(writefd)?;

        let timeout = Duration::from_millis(100);
        let threads = [
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
        ];

        let mut results = threads.map(|t| t.join().unwrap());

        // With edge-triggering, only one of the threads should have gotten an
        // event, but we don't know which one.  Sort results by number of events
        // received.
        results.sort_by(|lhs, rhs| lhs.events.len().cmp(&rhs.events.len()));

        let expected_mask = match (fd_type, use_rdhup, make_readable) {
            (FdType::Pipe, _, MakeReadable::Yes) => EpollFlags::EPOLLHUP | EpollFlags::EPOLLIN,
            (FdType::Pipe, _, MakeReadable::No) => EpollFlags::EPOLLHUP,
            (FdType::TcpStream, UseEPOLLRDHUP::Yes, _) => {
                EpollFlags::EPOLLRDHUP | EpollFlags::EPOLLIN
            }
            (FdType::TcpStream, UseEPOLLRDHUP::No, _) => EpollFlags::EPOLLIN,
        };

        match use_edge {
            UseEPOLLET::No => {
                // Both threads get the event
                ensure_ord!(results[0].epoll_res, ==, Ok(1));
                ensure_ord!(results[0].duration, <, timeout);
                ensure_ord!(results[0].events[0], ==, epoll::EpollEvent::new(expected_mask, 0));
            }
            UseEPOLLET::Yes => {
                // One thread should have timed out with no events received.
                ensure_ord!(results[0].epoll_res, ==, Ok(0));
                ensure_ord!(results[0].duration, >=, timeout);
            }
        }

        // The other should have gotten a single event.
        ensure_ord!(results[1].epoll_res, ==, Ok(1));
        ensure_ord!(results[1].duration, <, timeout);
        ensure_ord!(results[1].events[0], ==, epoll::EpollEvent::new(expected_mask, 0));
        Ok(())
    })
}

fn test_threads_level() -> anyhow::Result<()> {
    let (readfd, writefd) = unistd::pipe()?;
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        let timeout = Duration::from_millis(100);

        let threads = [
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
        ];

        // Wait for readers to block.
        std::thread::sleep(timeout / 2);

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        let results = threads.map(|t| t.join().unwrap());

        // Both waiters should have received the event
        for res in results {
            ensure_ord!(res.epoll_res, ==, Ok(1));
            ensure_ord!(res.duration, <, timeout);
            ensure_ord!(res.events[0], ==, epoll::EpollEvent::new(EpollFlags::EPOLLIN, 0));
        }

        Ok(())
    })
}

/// This test has the threads read from the pipe immediately after returning from `epoll_wait`.
///
/// In Linux, if the first thread to wake from `epoll_wait` reads from the pipe fast enough, the
/// other threads blocked on `epoll_wait` won't wake up. This is a race condition, but it means that
/// not all threads will necessarily be woken (unlike the `test_threads_level` test above). Since
/// Shadow doesn't switch threads until there is a blocking syscall, we expect that the thread which
/// first wakes from `epoll_wait` will read from the pipe before the other thread is woken from
/// `epoll_wait`, which means the second thread shouldn't be woken until the timeout expires.
///
/// The purpose of this test is to make sure Shadow replicates the Linux behaviour of sometimes
/// avoiding a spurious wakeup if the epoll status changes back to "not readable" before the thread
/// has a chance to run.  Code that uses epoll shouldn't rely on all `epoll_wait` calls being woken.
fn test_threads_level_with_late_read() -> anyhow::Result<()> {
    let (readfd, writefd) = unistd::pipe2(nix::fcntl::OFlag::O_NONBLOCK)?;
    let epollfd = epoll::epoll_create()?;
    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLIN, readfd as u64);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        let timeout = Duration::from_millis(100);

        let threads = [
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ true)),
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ true)),
        ];

        // Wait for readers to block.
        std::thread::sleep(timeout / 2);

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        let mut results = threads.map(|t| t.join().unwrap());

        // One of the threads should have gotten an event, but we don't know which one.
        // Sort results by number of events received.
        results.sort_by(|lhs, rhs| lhs.events.len().cmp(&rhs.events.len()));

        // One thread should have timed out with no events received.
        ensure_ord!(results[0].epoll_res, ==, Ok(0));
        ensure_ord!(results[0].duration, >=, timeout);

        // The other should have gotten a single event.
        ensure_ord!(results[1].epoll_res, ==, Ok(1));
        ensure_ord!(results[1].duration, <, timeout);
        ensure_ord!(results[1].events[0].events(), ==, EpollFlags::EPOLLIN);

        Ok(())
    })
}

/// This test has the main thread immediately read from the pipe after writing to it.
///
/// In Linux, if the condition that would wake threads from `epoll_wait` is triggered but
/// immediately "undone" before the blocked threads resume, then none of the blocked threads will be
/// woken (no spurious wakeups). Since Shadow doesn't switch threads until there is a blocking
/// syscall, we expect that the main thread will read from the pipe before any other thread is woken
/// from `epoll_wait`, which means that no threads should be woken until the timeout expires.
///
/// The purpose of this test is to make sure Shadow replicates the Linux behaviour of sometimes
/// avoiding a spurious wakeup if the epoll status changes back to "not readable" before the thread
/// has a chance to run.  Code that uses epoll shouldn't rely on `epoll_wait` calls being woken
/// every time the file status changes.
fn test_threads_level_with_early_read() -> anyhow::Result<()> {
    let (readfd, writefd) = unistd::pipe2(nix::fcntl::OFlag::O_NONBLOCK)?;
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLIN, readfd as u64);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        let timeout = Duration::from_millis(100);

        let threads = [
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
            std::thread::spawn(move || do_epoll_wait(epollfd, timeout, /* do_read= */ false)),
        ];

        // Wait for readers to block.
        std::thread::sleep(timeout / 2);

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        // Immediately make the read-end not-readable.
        unistd::read(readfd, &mut [0])?;

        let results = threads.map(|t| t.join().unwrap());

        // Neither waiter should have received the event
        for res in results {
            ensure_ord!(res.epoll_res, ==, Ok(0));
            ensure_ord!(res.duration, >=, timeout);
        }

        Ok(())
    })
}

fn test_wait_negative_timeout() -> anyhow::Result<()> {
    let (read_fd, write_fd) = unistd::pipe()?;
    let epoll_fd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epoll_fd, read_fd, write_fd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(
            epoll_fd,
            epoll::EpollOp::EpollCtlAdd,
            read_fd,
            Some(&mut event),
        )?;

        // first test epoll_wait and epoll_pwait

        // epoll_wait(2): "Specifying a timeout of -1 causes epoll_wait() to block indefinitely"
        // This seems to apply to all negative values, not just -1
        for timeout in [-1, -2] {
            let t = std::thread::spawn(move || {
                std::thread::sleep(Duration::from_millis(100));
                unistd::write(write_fd, &[0])
            });

            let mut events = Vec::new();
            events.resize(10, epoll::EpollEvent::empty());

            let res = epoll::epoll_wait(epoll_fd, &mut events, timeout)?;
            assert!(res > 0);

            assert_eq!(unistd::read(read_fd, &mut [0])?, 1);

            t.join().unwrap()?;
        }

        // next test epoll_pwait2

        let t = std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(100));
            unistd::write(write_fd, &[0])
        });

        let mut events = Vec::new();
        events.resize(10, libc::epoll_event { events: 0, u64: 0 });

        let timeout = libc::timespec {
            tv_sec: -1,
            tv_nsec: 0,
        };

        let res = Errno::result(unsafe {
            epoll_pwait2(
                epoll_fd,
                events.as_mut_ptr(),
                events.len() as libc::c_int,
                &timeout,
                std::ptr::null(),
            )
        });

        // negative timeouts for epoll_pwait2 should not be allowed
        // TODO: remove ENOSYS once all supported platforms use kernel >=5.11
        assert!(
            res == Err(Errno::EINVAL)
                || (!test_utils::running_in_shadow() && res == Err(Errno::ENOSYS))
        );

        assert_eq!(unistd::read(read_fd, &mut [0])?, 1);

        t.join().unwrap()?;

        Ok(())
    })
}

fn test_ctl_invalid_op() -> anyhow::Result<()> {
    let (read_fd, write_fd) = unistd::pipe()?;
    let epoll_fd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epoll_fd, read_fd, write_fd], || {
        let mut event = libc::epoll_event { events: 0, u64: 0 };

        // assume this is not used by Linux
        let operation = libc::c_int::MAX;

        let rv =
            Errno::result(unsafe { libc::epoll_ctl(epoll_fd, operation, read_fd, &mut event) });
        assert_eq!(rv, Err(Errno::EINVAL));

        Ok(())
    })
}

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];
    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![
        ShadowTest::new("threads-edge", test_threads_edge, all_envs.clone()),
        ShadowTest::new("threads-level", test_threads_level, all_envs.clone()),
        // in Linux these two tests have a race condition and don't always pass
        ShadowTest::new(
            "threads-level-with-late-read",
            test_threads_level_with_late_read,
            set![TestEnvironment::Shadow],
        ),
        ShadowTest::new(
            "threads-level-with-early-read",
            test_threads_level_with_early_read,
            set![TestEnvironment::Shadow],
        ),
        ShadowTest::new(
            "test_wait_negative_timeout",
            test_wait_negative_timeout,
            all_envs.clone(),
        ),
        ShadowTest::new("test_ctl_invalid_op", test_ctl_invalid_op, all_envs.clone()),
    ];
    for use_edge in [UseEPOLLET::Yes, UseEPOLLET::No] {
        for use_rdhup in [UseEPOLLRDHUP::Yes, UseEPOLLRDHUP::No] {
            for make_readable in [MakeReadable::Yes, MakeReadable::No] {
                for fd_type in [FdType::Pipe, FdType::TcpStream] {
                    let passing = match (fd_type, use_rdhup) {
                        (FdType::TcpStream, UseEPOLLRDHUP::No) => all_envs.clone(),
                        _ => set![TestEnvironment::Libc],
                    };
                    tests.push(ShadowTest::new(
                        &format!("threads-eof-edge:{use_edge:?}-rdhup:{use_rdhup:?}-readable:{make_readable:?}-type:{fd_type:?}"),
                        move || test_threads_eof(use_edge, use_rdhup, make_readable, fd_type),
                        passing,
                    ));
                }
            }
        }
    }
    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnvironment::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnvironment::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");

    Ok(())
}

unsafe fn epoll_pwait2(
    epfd: libc::c_int,
    events: *mut libc::epoll_event,
    maxevents: libc::c_int,
    timeout: *const libc::timespec,
    sigmask: *const libc::sigset_t,
) -> libc::c_int {
    unsafe {
        libc::syscall(
            libc::SYS_epoll_pwait2,
            epfd,
            events,
            maxevents,
            timeout,
            sigmask,
        ) as libc::c_int
    }
}
