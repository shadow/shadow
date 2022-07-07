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

fn do_epoll_wait(epoll_fd: i32, timeout: Duration) -> WaiterResult {
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
        std::thread::spawn(move || do_epoll_wait(epollfd, timeout)),
        std::thread::spawn(move || do_epoll_wait(epollfd, timeout)),
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
        std::thread::spawn(move || do_epoll_wait(epollfd, timeout)),
        std::thread::spawn(move || do_epoll_wait(epollfd, timeout)),
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
    ];

    if filter_shadow_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnvironment::Shadow))
            .collect()
    }
    if filter_libc_passing {
        tests = tests
            .into_iter()
            .filter(|x| x.passing(TestEnvironment::Libc))
            .collect()
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");

    Ok(())
}
