use std::io::Cursor;
use std::sync::mpsc;

use neli::ToBytes;
use neli::consts::nl::NlmF;
use neli::consts::rtnl::{RtAddrFamily, RtScope, Rtm};
use neli::nl::{NlPayload, NlmsghdrBuilder};
use neli::rtnl::IfaddrmsgBuilder;

use nix::sys::epoll::{self, EpollFlags};
use nix::unistd;

use test_utils::socket_utils::{SocketInitMethod, socket_init_helper};
use test_utils::{ShadowTest, TestEnvironment, ensure_ord, set};

use crate::util::*;

mod util;

fn test_multi_write(readfd: libc::c_int, writefd: libc::c_int) -> anyhow::Result<()> {
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        // We expect the first two will not reach the timeout, use LONG_DUR.
        let (waiter1, block1) = init(epollfd, LONG_DUR);
        let (waiter2, block2) = init(epollfd, LONG_DUR);
        // We expect the last one will reach the timeout, use SHORT_DUR.
        let (waiter3, block3) = init(epollfd, SHORT_DUR);

        let thread =
            std::thread::spawn(move || vec![waiter1.wait(), waiter2.wait(), waiter3.wait()]);

        // Wait for the thread to start and execute the first epoll_wait,
        // then write to the write end so the read end becomes readable.
        block1.wait();
        unistd::write(writefd, &[0])?;

        // Wait until inside the next epoll_wait, make the read-end readable again.
        block2.wait();
        unistd::write(writefd, &[0])?;

        block3.wait();
        let results = thread.join().unwrap();

        // The first two waits should have received the event
        for res in &results[..2] {
            ensure_ord!(res.epoll_res, ==, Ok(1));
            ensure_ord!(res.duration, <, LONG_DUR);
            ensure_ord!(res.events[0], ==, readable_zero());
        }

        // The last wait should have timed out with no events received.
        ensure_ord!(results[2].epoll_res, ==, Ok(0));
        ensure_ord!(results[2].duration, >=, SHORT_DUR);

        Ok(())
    })
}

fn test_write_then_partial_read(readfd: libc::c_int, writefd: libc::c_int) -> anyhow::Result<()> {
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        // We expect the first one will not reach the timeout, use LONG_DUR.
        let (waiter1, block1) = init(epollfd, LONG_DUR);
        // We expect the second one will reach the timeout, use SHORT_DUR.
        let (waiter2, block2) = init(epollfd, SHORT_DUR);

        let thread = std::thread::spawn(move || vec![waiter1.wait(), waiter2.wait()]);

        // Make the read-end readable.
        block1.wait();
        unistd::write(writefd, &[0, 0])?;

        // Wait and read some, but not all, from the buffer.
        block2.wait();
        unistd::read(readfd, &mut [0])?;

        let results = thread.join().unwrap();

        // The first wait should have received the event
        ensure_ord!(results[0].epoll_res, ==, Ok(1));
        ensure_ord!(results[0].duration, <, LONG_DUR);
        ensure_ord!(results[0].events[0], ==, readable_zero());

        // The second wait should have timed out with no events received.
        ensure_ord!(results[1].epoll_res, ==, Ok(0));
        ensure_ord!(results[1].duration, >=, SHORT_DUR);

        Ok(())
    })
}

fn test_threads_multi_write(readfd: libc::c_int, writefd: libc::c_int) -> anyhow::Result<()> {
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        // For communicating results from the spawned threads to the main thread.
        let (tx1, rx) = mpsc::sync_channel(3);
        let (tx2, tx3) = (tx1.clone(), tx1.clone());

        let (waiter1, block1) = init(epollfd, SHORT_DUR);
        let (waiter2, _block2) = init(epollfd, SHORT_DUR);
        let (waiter3, _block3) = init(epollfd, SHORT_DUR);

        let threads = [
            std::thread::spawn(move || tx1.send(waiter1.wait()).unwrap()),
            std::thread::spawn(move || tx2.send(waiter2.wait()).unwrap()),
            std::thread::spawn(move || tx3.send(waiter3.wait()).unwrap()),
        ];

        // Wait for at least one thread to be ready.
        block1.wait();

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        // Wait for the event to be collected.
        let result1 = rx.recv().unwrap();

        // Make the read-end readable again and collect the result.
        unistd::write(writefd, &[0])?;
        let result2 = rx.recv().unwrap();

        // Collect the final event (probably the timeout).
        let result3 = rx.recv().unwrap();

        let _ = threads.map(|t| t.join().unwrap());
        let mut results = [result1, result2, result3];

        // Two of the threads should have gotten an event, but we don't know which one.
        // Sort results by number of events received.
        results.sort_by_key(|e| e.events.len());

        // One thread should have timed out with no events received.
        ensure_ord!(results[0].epoll_res, ==, Ok(0));
        ensure_ord!(results[0].duration, >=, SHORT_DUR);

        // The rest should have received the event
        for res in &results[1..] {
            ensure_ord!(res.epoll_res, ==, Ok(1));
            ensure_ord!(res.duration, <, SHORT_DUR);
            ensure_ord!(res.events[0], ==, readable_zero());
        }

        Ok(())
    })
}

fn test_oneshot_multi_write(readfd: libc::c_int, writefd: libc::c_int) -> anyhow::Result<()> {
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, readfd, writefd], || {
        let mut event = epoll::EpollEvent::new(
            EpollFlags::EPOLLONESHOT | EpollFlags::EPOLLET | EpollFlags::EPOLLIN,
            0,
        );
        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlAdd,
            readfd,
            Some(&mut event),
        )?;

        // We expect the first and last will not reach the timeout, use LONG_DUR.
        let (waiter1, block1) = init(epollfd, LONG_DUR);
        let (waiter2, block2) = init(epollfd, SHORT_DUR);
        let (waiter3, block3) = init(epollfd, LONG_DUR);

        let thread =
            std::thread::spawn(move || vec![waiter1.wait(), waiter2.wait(), waiter3.wait()]);

        // Wait for readers to block.
        block1.wait();

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        // Wait again and make the read-end readable again.
        block2.wait();
        unistd::write(writefd, &[0])?;

        // Wait for the second wait to time out.
        block3.wait();

        epoll::epoll_ctl(
            epollfd,
            epoll::EpollOp::EpollCtlMod,
            readfd,
            Some(&mut event),
        )?;

        // Make the read-end readable.
        unistd::write(writefd, &[0])?;

        let results = thread.join().unwrap();

        // The first wait should have received the event
        ensure_ord!(results[0].epoll_res, ==, Ok(1));
        ensure_ord!(results[0].duration, <, LONG_DUR);
        ensure_ord!(results[0].events[0], ==, readable_zero());

        // The second wait should have timed out with no events received.
        ensure_ord!(results[1].epoll_res, ==, Ok(0));
        ensure_ord!(results[1].duration, >=, SHORT_DUR);

        // The third wait should have received the event
        ensure_ord!(results[2].epoll_res, ==, Ok(1));
        ensure_ord!(results[2].duration, <, LONG_DUR);
        ensure_ord!(results[2].events[0], ==, readable_zero());

        Ok(())
    })
}

fn test_eventfd_multi_write() -> anyhow::Result<()> {
    let efd =
        test_utils::check_system_call!(|| unsafe { libc::eventfd(0, libc::EFD_NONBLOCK) }, &[])
            .unwrap();
    let epollfd = epoll::epoll_create()?;

    test_utils::run_and_close_fds(&[epollfd, efd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(epollfd, epoll::EpollOp::EpollCtlAdd, efd, Some(&mut event))?;

        // We expect the first three will not reach the timeout, use LONG_DUR.
        let (waiter1, block1) = init(epollfd, LONG_DUR);
        let (waiter2, block2) = init(epollfd, LONG_DUR);
        let (waiter3, block3) = init(epollfd, LONG_DUR);
        let (waiter4, block4) = init(epollfd, SHORT_DUR);

        let thread = std::thread::spawn(move || {
            vec![
                waiter1.wait(),
                waiter2.wait(),
                waiter3.wait(),
                waiter4.wait(),
            ]
        });

        // Wait for readers to block.
        block1.wait();

        // Make the read-end readable.
        unistd::write(efd, &1u64.to_le_bytes())?;

        // Wait again and make the read-end readable again.
        block2.wait();
        unistd::write(efd, &1u64.to_le_bytes())?;

        // Wait again and make the read-end readable again, but with zero value this time.
        block3.wait();
        unistd::write(efd, &0u64.to_le_bytes())?;

        block4.wait();
        let results = thread.join().unwrap();

        // The first three waits should have received the event
        for res in &results[..3] {
            ensure_ord!(res.epoll_res, ==, Ok(1));
            ensure_ord!(res.duration, <, LONG_DUR);
            ensure_ord!(res.events[0], ==, readable_zero());
        }

        // The last wait should have timed out with no events received.
        ensure_ord!(results[3].epoll_res, ==, Ok(0));
        ensure_ord!(results[3].duration, >=, SHORT_DUR);

        Ok(())
    })
}

fn test_netlink_multi_write() -> anyhow::Result<()> {
    let fd = unsafe {
        libc::socket(
            libc::AF_NETLINK,
            libc::SOCK_RAW | libc::SOCK_NONBLOCK,
            libc::NETLINK_ROUTE,
        )
    };
    let epollfd = epoll::epoll_create()?;

    let buffer = {
        let ifaddrmsg = IfaddrmsgBuilder::default()
            .ifa_family(RtAddrFamily::Unspecified)
            .ifa_prefixlen(0)
            .ifa_scope(RtScope::Universe)
            .ifa_index(0)
            .build()
            .unwrap();
        let nlmsg = NlmsghdrBuilder::default()
            .nl_type(Rtm::Getaddr)
            .nl_flags(NlmF::REQUEST | NlmF::DUMP)
            .nl_seq(0xfe182ab9) // Random number
            .nl_payload(NlPayload::Payload(ifaddrmsg))
            .build()
            .unwrap();

        let mut buffer = Cursor::new(Vec::new());
        nlmsg.to_bytes(&mut buffer).unwrap();
        buffer.into_inner()
    };

    test_utils::run_and_close_fds(&[epollfd, fd], || {
        let mut event = epoll::EpollEvent::new(EpollFlags::EPOLLET | EpollFlags::EPOLLIN, 0);
        epoll::epoll_ctl(epollfd, epoll::EpollOp::EpollCtlAdd, fd, Some(&mut event))?;

        // We expect the first two will not reach the timeout, use LONG_DUR.
        let (waiter1, block1) = init(epollfd, LONG_DUR);
        let (waiter2, block2) = init(epollfd, LONG_DUR);
        // We expect the last one will reach the timeout, use SHORT_DUR.
        let (waiter3, block3) = init(epollfd, SHORT_DUR);

        let thread =
            std::thread::spawn(move || vec![waiter1.wait(), waiter2.wait(), waiter3.wait()]);

        // Wait for readers to block.
        block1.wait();

        // Make the read-end readable.
        unistd::write(fd, buffer.as_slice())?;

        // Wait again and make the read-end readable again.
        block2.wait();
        unistd::write(fd, buffer.as_slice())?;

        block3.wait();
        let results = thread.join().unwrap();

        // The first two waits should have received the event
        for res in &results[..2] {
            ensure_ord!(res.epoll_res, ==, Ok(1));
            ensure_ord!(res.duration, <, LONG_DUR);
            ensure_ord!(res.events[0], ==, readable_zero());
        }

        // The last wait should have timed out with no events received.
        ensure_ord!(results[2].epoll_res, ==, Ok(0));
        ensure_ord!(results[2].duration, >=, SHORT_DUR);

        Ok(())
    })
}

fn socket_init(
    init_method: SocketInitMethod,
    sock_type: libc::c_int,
) -> (libc::c_int, libc::c_int) {
    let (fd_client, fd_server) = socket_init_helper(
        init_method,
        sock_type,
        libc::SOCK_NONBLOCK,
        /* bind_client = */ false,
    );
    (fd_server, fd_client)
}

fn tcp_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::Inet, libc::SOCK_STREAM)
}
fn udp_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::Inet, libc::SOCK_DGRAM)
}

fn unix_stream_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::Unix, libc::SOCK_STREAM)
}
fn unix_dgram_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::Unix, libc::SOCK_DGRAM)
}
fn unix_seqpacket_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::Unix, libc::SOCK_SEQPACKET)
}

fn unix_pair_stream_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::UnixSocketpair, libc::SOCK_STREAM)
}
fn unix_pair_dgram_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::UnixSocketpair, libc::SOCK_DGRAM)
}
fn unix_pair_seqpacket_fds_init_helper() -> (libc::c_int, libc::c_int) {
    socket_init(SocketInitMethod::UnixSocketpair, libc::SOCK_SEQPACKET)
}

fn pipe_fds_init_helper() -> (libc::c_int, libc::c_int) {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )
    .unwrap();
    (fds[0], fds[1])
}

fn pipe_direct_fds_init_helper() -> (libc::c_int, libc::c_int) {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )
    .unwrap();
    (fds[0], fds[1])
}

fn main() -> anyhow::Result<()> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnvironment::Libc, TestEnvironment::Shadow];
    let mut tests: Vec<test_utils::ShadowTest<(), anyhow::Error>> = vec![];

    let mut add_tests =
        |name: &str, swappable: bool, fds_init_helper: fn() -> (libc::c_int, libc::c_int)| {
            // add details to the test names to avoid duplicates
            let append_args = |s, swapped: bool| {
                if swappable {
                    format!("{s} <fds={name:?},swapped={swapped:?}>")
                } else {
                    format!("{s} <fds={name:?}>")
                }
            };

            for &swapped in if swappable {
                &[false, true][..]
            } else {
                &[false][..]
            } {
                let extend_test = |test: fn(libc::c_int, libc::c_int) -> anyhow::Result<()>| {
                    move || {
                        let (fd1, fd2) = fds_init_helper();
                        let (readfd, writefd) = if swapped { (fd2, fd1) } else { (fd1, fd2) };
                        test(readfd, writefd)
                    }
                };
                tests.extend(vec![
                    ShadowTest::new(
                        &append_args("multi-write", swapped),
                        extend_test(test_multi_write),
                        all_envs.clone(),
                    ),
                    ShadowTest::new(
                        &append_args("oneshot-multi-write", swapped),
                        extend_test(test_oneshot_multi_write),
                        all_envs.clone(),
                    ),
                    ShadowTest::new(
                        &append_args("write-then-partial-read", swapped),
                        extend_test(test_write_then_partial_read),
                        all_envs.clone(),
                    ),
                    ShadowTest::new(
                        &append_args("threads-multi-write", swapped),
                        extend_test(test_threads_multi_write),
                        all_envs.clone(),
                    ),
                ]);
            }
        };

    add_tests("tcp", /* swappable = */ true, tcp_fds_init_helper);
    add_tests("udp", /* swappable = */ false, udp_fds_init_helper);
    add_tests(
        "unix-stream",
        /* swappable = */ true,
        unix_stream_fds_init_helper,
    );
    add_tests(
        "unix-dgram",
        /* swappable = */ false,
        unix_dgram_fds_init_helper,
    );
    add_tests(
        "unix-seqpacket",
        /* swappable = */ true,
        unix_seqpacket_fds_init_helper,
    );
    add_tests(
        "unix-pair-stream",
        /* swappable = */ true,
        unix_pair_stream_fds_init_helper,
    );
    add_tests(
        "unix-pair-dgram",
        /* swappable = */ false,
        unix_pair_dgram_fds_init_helper,
    );
    add_tests(
        "unix-pair-seqpacket",
        /* swappable = */ true,
        unix_pair_seqpacket_fds_init_helper,
    );
    add_tests("pipe", /* swappable = */ false, pipe_fds_init_helper);
    add_tests(
        "pipe-direct",
        /* swappable = */ false,
        pipe_direct_fds_init_helper,
    );

    // add the test only for eventfd, since it supports only reading/writing 8-byte integers
    tests.push(ShadowTest::new(
        "eventfd-multi-write",
        test_eventfd_multi_write,
        all_envs.clone(),
    ));
    // add the test only for netlink sockets, since it is a one-end socket
    tests.push(ShadowTest::new(
        "netlink-multi-write",
        test_netlink_multi_write,
        all_envs.clone(),
    ));

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
