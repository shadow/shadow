use std::time::Duration;

use nix::{
    sys::epoll::{self, EpollFlags},
    unistd,
};
use test_utils::{ensure_ord, set, ShadowTest, TestEnvironment};

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
}

fn test_threads_level() -> anyhow::Result<()> {
    let (readfd, writefd) = unistd::pipe()?;
    let epollfd = epoll::epoll_create()?;

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
        ShadowTest::new("threads-level", test_threads_level, all_envs),
        // in Linux these tests have a race condition and don't always pass
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
    ];

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
