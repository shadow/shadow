/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::time::Duration;

use nix::poll::PollFlags;
use test_utils::TestEnvironment as TestEnv;
use test_utils::{iov_helper, iov_helper_mut, set};

fn main() -> Result<(), String> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let mut tests = get_tests();
    if filter_shadow_passing {
        tests.retain(|x| x.passing(TestEnv::Shadow));
    }
    if filter_libc_passing {
        tests.retain(|x| x.passing(TestEnv::Libc));
    }

    test_utils::run_tests(&tests, summarize)?;

    println!("Success.");
    Ok(())
}

fn get_tests() -> Vec<test_utils::ShadowTest<(), String>> {
    let tests: Vec<test_utils::ShadowTest<_, _>> = vec![
        test_utils::ShadowTest::new("test_null", test_null, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new("test_pipe", test_pipe, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_read_write",
            test_read_write,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_readv_writev",
            test_readv_writev,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_large_read_write",
            test_large_read_write,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_read_write_empty",
            test_read_write_empty,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_write_to_read_end",
            test_write_to_read_end,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_read_from_write_end",
            test_read_from_write_end,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_get_size",
            test_get_size,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_read_after_write_close_with_empty_buffer",
            test_read_after_write_close_with_empty_buffer,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_read_after_write_close_with_nonempty_buffer",
            test_read_after_write_close_with_nonempty_buffer,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_write_after_read_close_with_full_buffer",
            test_write_after_read_close_with_full_buffer,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_write_after_read_close_with_nonfull_buffer",
            test_write_after_read_close_with_nonfull_buffer,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_large_packet",
            test_o_direct_large_packet,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_large_packet_vectored",
            test_o_direct_large_packet_vectored,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_read_fd",
            test_o_direct_read_fd,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_write_fd",
            test_o_direct_write_fd,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_stream_to_packet_mode_empty",
            test_o_direct_stream_to_packet_mode_empty,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_packet_to_stream_mode_empty",
            test_o_direct_packet_to_stream_mode_empty,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_stream_to_packet_mode_nonempty",
            test_o_direct_stream_to_packet_mode_nonempty,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_packet_to_stream_mode_nonempty",
            test_o_direct_packet_to_stream_mode_nonempty,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_full_buffer_1",
            test_o_direct_full_buffer_1,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_o_direct_full_buffer_2",
            test_o_direct_full_buffer_2,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_close_during_blocking_read",
            test_close_during_blocking_read,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_close_during_blocking_write",
            test_close_during_blocking_write,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    tests
}

fn test_null() -> Result<(), String> {
    test_utils::check_system_call!(
        || { unsafe { libc::pipe(std::ptr::null_mut()) } },
        &[libc::EFAULT]
    )?;
    Ok(())
}

fn test_pipe() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    Ok(())
}

fn test_read_write() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_buf = [1u8, 2, 3, 4];

        let rv = test_utils::check_system_call!(
            || {
                unsafe {
                    libc::write(
                        write_fd,
                        write_buf.as_ptr() as *const libc::c_void,
                        write_buf.len(),
                    )
                }
            },
            &[]
        )?;

        test_utils::result_assert_eq(rv, 4, "Expected to write 4 bytes")?;

        let mut read_buf = [0u8; 4];

        let rv = test_utils::check_system_call!(
            || {
                unsafe {
                    libc::read(
                        read_fd,
                        read_buf.as_mut_ptr() as *mut libc::c_void,
                        read_buf.len(),
                    )
                }
            },
            &[]
        )?;

        test_utils::result_assert_eq(rv, 4, "Expected to read 4 bytes")?;

        test_utils::result_assert_eq(write_buf, read_buf, "Buffers differ")?;

        Ok(())
    })
}

fn test_readv_writev() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_iovs = iov_helper([&[1, 2, 3, 4][..], &[5, 6][..], &[][..], &[7, 8, 9][..]]);

        let rv = test_utils::check_system_call!(
            || {
                unsafe {
                    libc::writev(
                        write_fd,
                        write_iovs.as_ptr() as *const libc::iovec,
                        write_iovs.len() as i32,
                    )
                }
            },
            &[]
        )?;

        test_utils::result_assert_eq(rv, 9, "Expected to write 9 bytes")?;

        let read_iovs = [
            &mut [0; 2][..],
            &mut [0; 1][..],
            &mut [][..],
            &mut [0; 6][..],
        ];
        let mut read_iovs = iov_helper_mut(read_iovs);

        let rv = test_utils::check_system_call!(
            || {
                unsafe {
                    libc::readv(
                        read_fd,
                        read_iovs.as_mut_ptr() as *const libc::iovec,
                        read_iovs.len() as i32,
                    )
                }
            },
            &[]
        )?;

        test_utils::result_assert_eq(rv, 9, "Expected to read 9 bytes")?;

        let write_iter = write_iovs.iter().flat_map(std::ops::Deref::deref);
        let read_iter = read_iovs.iter().flat_map(std::ops::Deref::deref);
        test_utils::result_assert(write_iter.eq(read_iter), "Buffers differ")?;

        Ok(())
    })
}

fn test_large_read_write() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let mut write_buf = Vec::<u8>::with_capacity(8096 * 2);
        for _ in 0..write_buf.capacity() {
            let random_value = unsafe { libc::rand() };
            write_buf.push(random_value as u8)
        }

        let mut read_buf = vec![0u8; write_buf.len()];

        let mut bytes_written = 0;
        let mut bytes_read = 0;

        while bytes_read < write_buf.len() {
            let write_slice = &write_buf[bytes_written..];
            let towrite = write_slice.len();
            let rv = test_utils::check_system_call!(
                || {
                    unsafe {
                        libc::write(
                            write_fd,
                            write_slice.as_ptr() as *const libc::c_void,
                            towrite,
                        )
                    }
                },
                &[]
            )?;
            println!("Wrote {}", rv);
            bytes_written += rv as usize;

            let read_slice = &mut read_buf[bytes_read..];
            let toread = read_slice.len();
            let rv = test_utils::check_system_call!(
                || {
                    unsafe { libc::read(read_fd, read_slice.as_ptr() as *mut libc::c_void, toread) }
                },
                &[]
            )?;
            println!("Read {}", rv);
            let range_read = bytes_read..bytes_read + rv as usize;
            assert_eq!(read_buf[range_read.clone()], write_buf[range_read]);
            bytes_read += rv as usize;
        }

        Ok(())
    })
}

// pipe(2) indicates that size zero writes to pipes with O_DIRECT are no-ops,
// and somewhat implies that they are no-ops without it as well. Exerimentally
// size zero reads and writes to pipes are both no-ops.
fn test_read_write_empty() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let rv = test_utils::check_system_call!(
            || { unsafe { libc::write(write_fd, std::ptr::null(), 0,) } },
            &[]
        )?;
        test_utils::result_assert_eq(rv, 0, "Expected to write 0 bytes")?;

        let rv = test_utils::check_system_call!(
            || { unsafe { libc::read(read_fd, std::ptr::null_mut(), 0,) } },
            &[]
        )?;
        test_utils::result_assert_eq(rv, 0, "Expected to read 0 bytes")?;

        // Reading again should still succeed and not block. There are no "0
        // byte datagrams" with pipes; reading and writing 0 bytes is just a
        // no-op.
        let rv = test_utils::check_system_call!(
            || { unsafe { libc::read(read_fd, std::ptr::null_mut(), 0,) } },
            &[]
        )?;
        test_utils::result_assert_eq(rv, 0, "Expected to read 0 bytes")?;

        Ok(())
    })
}

fn test_write_to_read_end() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_buf = [1u8, 2, 3, 4];

        test_utils::check_system_call!(
            || {
                unsafe {
                    libc::write(
                        read_fd,
                        write_buf.as_ptr() as *const libc::c_void,
                        write_buf.len(),
                    )
                }
            },
            &[libc::EBADF]
        )?;

        Ok(())
    })
}

fn test_read_from_write_end() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let write_buf = [1u8, 2, 3, 4];

        let rv = test_utils::check_system_call!(
            || {
                unsafe {
                    libc::write(
                        write_fd,
                        write_buf.as_ptr() as *const libc::c_void,
                        write_buf.len(),
                    )
                }
            },
            &[]
        )?;

        test_utils::result_assert_eq(rv, 4, "Expected to write 4 bytes")?;

        let mut read_buf = [0u8; 4];

        test_utils::check_system_call!(
            || {
                unsafe {
                    libc::read(
                        write_fd,
                        read_buf.as_mut_ptr() as *mut libc::c_void,
                        read_buf.len(),
                    )
                }
            },
            &[libc::EBADF]
        )?;

        Ok(())
    })
}

fn test_get_size() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe(fds.as_mut_ptr()) } }, &[])?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        let size = test_utils::check_system_call!(
            || unsafe { libc::fcntl(read_fd, libc::F_GETPIPE_SZ) },
            &[]
        )?;
        assert!(size > 0);

        Ok(())
    })
}

fn test_read_after_write_close_with_empty_buffer() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    assert!(fds[0] > 0, "fds[0] not set");
    assert!(fds[1] > 0, "fds[1] not set");

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[read_fd], || {
        let mut buf = vec![0u8; 10];

        test_utils::run_and_close_fds(&[write_fd], || {
            assert_eq!(
                nix::unistd::read(read_fd, &mut buf).unwrap_err(),
                nix::errno::Errno::EWOULDBLOCK
            );

            nix::unistd::write(write_fd, &[1, 2, 3]).unwrap();
            assert_eq!(nix::unistd::read(read_fd, &mut buf).unwrap(), 3);
            assert_eq!(
                nix::unistd::read(read_fd, &mut buf).unwrap_err(),
                nix::errno::Errno::EWOULDBLOCK
            );
        });

        // read fd should be POLLHUP
        assert_eq!(
            test_utils::poll_status(read_fd, 0).unwrap(),
            // shadow doesn't support POLLHUP
            // https://github.com/shadow/shadow/issues/2181
            if test_utils::running_in_shadow() {
                PollFlags::POLLIN
            } else {
                PollFlags::POLLHUP
            }
        );

        // the write fd is closed, so reading should return 0
        assert_eq!(nix::unistd::read(read_fd, &mut buf).unwrap(), 0);
        assert_eq!(nix::unistd::read(read_fd, &mut buf).unwrap(), 0);
    });

    Ok(())
}

fn test_read_after_write_close_with_nonempty_buffer() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    assert!(fds[0] > 0, "fds[0] not set");
    assert!(fds[1] > 0, "fds[1] not set");

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[read_fd], || {
        let mut buf = vec![0u8; 10];

        test_utils::run_and_close_fds(&[write_fd], || {
            assert_eq!(
                nix::unistd::read(read_fd, &mut buf).unwrap_err(),
                nix::errno::Errno::EWOULDBLOCK
            );

            nix::unistd::write(write_fd, &[1, 2, 3]).unwrap();
        });

        // read fd should be readable
        assert!(test_utils::is_readable(read_fd, 0).unwrap());

        // the write fd is closed, but there are still bytes remaining
        assert_eq!(nix::unistd::read(read_fd, &mut buf).unwrap(), 3);

        // read fd should be POLLHUP
        assert_eq!(
            test_utils::poll_status(read_fd, 0).unwrap(),
            // shadow doesn't support POLLHUP
            // https://github.com/shadow/shadow/issues/2181
            if test_utils::running_in_shadow() {
                PollFlags::POLLIN
            } else {
                PollFlags::POLLHUP
            }
        );

        // the write fd is closed, so reading should return 0
        assert_eq!(nix::unistd::read(read_fd, &mut buf).unwrap(), 0);
        assert_eq!(nix::unistd::read(read_fd, &mut buf).unwrap(), 0);
    });

    Ok(())
}

fn test_write_after_read_close_with_full_buffer() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    assert!(fds[0] > 0, "fds[0] not set");
    assert!(fds[1] > 0, "fds[1] not set");

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd], || {
        // write until buffer is full
        loop {
            let rv = nix::unistd::write(write_fd, &[1; 500]);
            if let Err(e) = rv {
                assert_eq!(e, nix::errno::Errno::EAGAIN);
                break;
            }
        }

        // write fd should not be writable
        assert!(!test_utils::is_writable(write_fd, 0).unwrap());

        // close the read fd
        nix::unistd::close(read_fd).unwrap();

        // write fd should be POLLERR
        assert_eq!(
            test_utils::poll_status(write_fd, 0).unwrap(),
            // shadow doesn't support POLLERR
            // https://github.com/shadow/shadow/issues/2181
            if test_utils::running_in_shadow() {
                PollFlags::POLLOUT
            } else {
                PollFlags::POLLERR
            }
        );

        // the read fd is closed, so writing should return EPIPE
        assert_eq!(
            nix::unistd::write(write_fd, &[1, 2, 3]),
            Err(nix::errno::Errno::EPIPE)
        );

        // write fd should be POLLERR
        assert_eq!(
            test_utils::poll_status(write_fd, 0).unwrap(),
            // shadow doesn't support POLLERR
            // https://github.com/shadow/shadow/issues/2181
            if test_utils::running_in_shadow() {
                PollFlags::POLLOUT
            } else {
                PollFlags::POLLERR
            }
        );
    });

    Ok(())
}

fn test_write_after_read_close_with_nonfull_buffer() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    assert!(fds[0] > 0, "fds[0] not set");
    assert!(fds[1] > 0, "fds[1] not set");

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd], || {
        // write, and then close the read fd
        test_utils::run_and_close_fds(&[read_fd], || {
            nix::unistd::write(write_fd, &[1, 2, 3]).unwrap();
        });

        // write fd should be writable
        assert!(test_utils::is_writable(write_fd, 0).unwrap());

        // the read fd is closed, so writing should return EPIPE
        assert_eq!(
            nix::unistd::write(write_fd, &[1, 2, 3]),
            Err(nix::errno::Errno::EPIPE)
        );

        // write fd should be writable
        assert!(test_utils::is_writable(write_fd, 0).unwrap());
    });

    Ok(())
}

// when writing large packets, they should be broken up into PIPE_BUF-sized packets
fn test_o_direct_large_packet() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // write a packet with length 1.5 * PIPE_BUF
        let packet = vec![0; libc::PIPE_BUF + (libc::PIPE_BUF / 2)];
        let len = nix::unistd::write(write_fd, &packet).unwrap();
        assert_eq!(len, packet.len());

        let mut in_buf = vec![0u8; packet.len()];

        // read one packet of size 1 * PIPE_BUF
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, libc::PIPE_BUF);

        // read one packet of size 0.5 * PIPE_BUF
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, libc::PIPE_BUF / 2);

        // no packets left
        assert_eq!(
            nix::unistd::read(read_fd, &mut in_buf).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );
    });

    Ok(())
}

// when writing large packets, they should be broken up into PIPE_BUF-sized packets
fn test_o_direct_large_packet_vectored() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // Write a packet with length 1.5 * PIPE_BUF. Bytes that cycle from 0 to 79 make it easy to
        // later verify the bytes survived the round-trip.
        let packet: Vec<u8> = (0..79)
            .cycle()
            .take(libc::PIPE_BUF + (libc::PIPE_BUF / 2))
            .collect();

        // split the packet into four iovecs
        let iovs = iov_helper([&packet[..100], &packet[100..110], &[][..], &packet[110..]]);

        // write everything
        let len = nix::sys::uio::writev(write_fd, &iovs).unwrap();
        assert_eq!(len, packet.len());

        let mut in_buf = vec![0u8; packet.len()];

        // helper to split the buffer slice into three slices
        fn split(buf: &mut [u8]) -> (&mut [u8], &mut [u8], &mut [u8]) {
            let buf = buf.split_at_mut(50);
            let buf = (buf.0, buf.1.split_at_mut(libc::PIPE_BUF));
            (buf.0, buf.1.0, buf.1.1)
        }

        // split the read buffer into four different iovecs
        let in_buf_split = split(&mut in_buf);
        let mut iovs =
            iov_helper_mut([in_buf_split.0, &mut [][..], in_buf_split.1, in_buf_split.2]);

        // read one packet of size 1 * PIPE_BUF
        let len = nix::sys::uio::readv(read_fd, &mut iovs).unwrap();
        assert_eq!(len, libc::PIPE_BUF);
        assert_eq!(&in_buf[..len], &packet[..len]);

        // split the read buffer into four different iovecs
        let in_buf_split = split(&mut in_buf);
        let mut iovs =
            iov_helper_mut([in_buf_split.0, &mut [][..], in_buf_split.1, in_buf_split.2]);

        // read one packet of size 0.5 * PIPE_BUF
        let len = nix::sys::uio::readv(read_fd, &mut iovs).unwrap();
        assert_eq!(len, libc::PIPE_BUF / 2);
        assert_eq!(&in_buf[..len], &packet[libc::PIPE_BUF..][..len]);

        // split the read buffer into four different iovecs
        let in_buf_split = split(&mut in_buf);
        let mut iovs =
            iov_helper_mut([in_buf_split.0, &mut [][..], in_buf_split.1, in_buf_split.2]);

        // no packets left
        assert_eq!(
            nix::sys::uio::readv(read_fd, &mut iovs).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );
    });

    Ok(())
}

// setting the O_DIRECT flag on the read fd has no effect
fn test_o_direct_read_fd() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // change only the read fd to O_DIRECT
        assert_eq!(
            unsafe { libc::fcntl(read_fd, libc::F_SETFL, libc::O_NONBLOCK | libc::O_DIRECT) },
            0
        );

        nix::unistd::write(write_fd, &[1, 2]).unwrap();
        nix::unistd::write(write_fd, &[3, 4, 5]).unwrap();

        let mut in_buf = vec![0u8; 5];

        // reads all 5 bytes at once (stream mode)
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 5);

        // no packets left
        assert_eq!(
            nix::unistd::read(read_fd, &mut in_buf).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );
    });

    Ok(())
}

// setting the O_DIRECT flag on the write fd causes future writes to be in packet mode
fn test_o_direct_write_fd() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // change only the write fd to O_DIRECT
        assert_eq!(
            unsafe { libc::fcntl(write_fd, libc::F_SETFL, libc::O_NONBLOCK | libc::O_DIRECT) },
            0
        );

        nix::unistd::write(write_fd, &[1, 2]).unwrap();
        nix::unistd::write(write_fd, &[3, 4, 5]).unwrap();

        let mut in_buf = vec![0u8; 5];

        // reads only 2 bytes (packet mode)
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        // reads only 3 bytes (packet mode)
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        // no packets left
        assert_eq!(
            nix::unistd::read(read_fd, &mut in_buf).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );
    });

    Ok(())
}

fn test_o_direct_stream_to_packet_mode_empty() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // write 5 bytes in stream mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        // switch to packet mode
        test_utils::check_system_call!(
            || unsafe { libc::fcntl(write_fd, libc::F_SETFL, libc::O_NONBLOCK | libc::O_DIRECT) },
            &[]
        )?;

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        let mut in_buf = [0u8; 2];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        // make sure the pipe is empty
        let mut in_buf = [0u8; 1];
        assert_eq!(
            nix::unistd::read(read_fd, &mut in_buf).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );

        // write 5 bytes in packet mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        let mut in_buf = [0u8; 2];
        assert_eq!(
            nix::unistd::read(read_fd, &mut in_buf).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );

        Ok(())
    })
}

fn test_o_direct_packet_to_stream_mode_empty() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // write 5 bytes in packet mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        // switch to stream mode
        test_utils::check_system_call!(
            || unsafe { libc::fcntl(write_fd, libc::F_SETFL, libc::O_NONBLOCK) },
            &[]
        )?;

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        // make sure the pipe is empty
        let mut in_buf = [0u8; 1];
        assert_eq!(
            nix::unistd::read(read_fd, &mut in_buf).err(),
            Some(nix::errno::Errno::EWOULDBLOCK)
        );

        // write 5 bytes in stream mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        let mut in_buf = [0u8; 2];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        Ok(())
    })
}

fn test_o_direct_stream_to_packet_mode_nonempty() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // write 5 bytes in stream mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        // switch to packet mode
        test_utils::check_system_call!(
            || unsafe { libc::fcntl(write_fd, libc::F_SETFL, libc::O_NONBLOCK | libc::O_DIRECT) },
            &[]
        )?;

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        // write 5 bytes in stream mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        let mut in_buf = [0u8; 2];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        let mut in_buf = [0u8; 2];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        Ok(())
    })
}

fn test_o_direct_packet_to_stream_mode_nonempty() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        // write 5 bytes in packet mode
        nix::unistd::write(write_fd, &[1, 2, 3]).unwrap();
        nix::unistd::write(write_fd, &[4, 5]).unwrap();

        // switch to stream mode
        test_utils::check_system_call!(
            || unsafe { libc::fcntl(write_fd, libc::F_SETFL, libc::O_NONBLOCK) },
            &[]
        )?;

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        // write 5 bytes in stream mode
        nix::unistd::write(write_fd, &[1, 2, 3, 4, 5]).unwrap();

        let mut in_buf = [0u8; 2];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        let mut in_buf = [0u8; 3];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 3);

        let mut in_buf = [0u8; 2];
        let len = nix::unistd::read(read_fd, &mut in_buf).unwrap();
        assert_eq!(len, 2);

        Ok(())
    })
}

fn test_o_direct_full_buffer_1() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        use nix::fcntl::FcntlArg;

        // get the pipe capacity
        let capacity = nix::fcntl::fcntl(read_fd, FcntlArg::F_GETPIPE_SZ).unwrap() as usize;

        // fill most of the pipe, with more than PIPE_BUF space remaining
        let buffer = vec![0u8; capacity - (libc::PIPE_BUF * 2)];
        let rv = nix::unistd::write(write_fd, &buffer).unwrap();
        assert_eq!(rv, buffer.len());

        // write a packet that would overflow the pipe, and there is more than PIPE_BUF space
        // remaining in the pipe
        let buffer = vec![0u8; libc::PIPE_BUF * 4];
        let rv = nix::unistd::write(write_fd, &buffer).unwrap();

        // a partial packet is written
        assert!(rv < buffer.len());

        Ok(())
    })
}

fn test_o_direct_full_buffer_2() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(
        || { unsafe { libc::pipe2(fds.as_mut_ptr(), libc::O_NONBLOCK | libc::O_DIRECT) } },
        &[]
    )?;

    test_utils::result_assert(fds[0] > 0, "fds[0] not set")?;
    test_utils::result_assert(fds[1] > 0, "fds[1] not set")?;

    let (read_fd, write_fd) = (fds[0], fds[1]);

    test_utils::run_and_close_fds(&[write_fd, read_fd], || {
        use nix::fcntl::FcntlArg;

        // get the pipe capacity
        let capacity = nix::fcntl::fcntl(read_fd, FcntlArg::F_GETPIPE_SZ).unwrap() as usize;

        // fill most of the pipe, with less than PIPE_BUF space remaining
        let buffer = vec![0u8; capacity - (libc::PIPE_BUF / 2)];
        let rv = nix::unistd::write(write_fd, &buffer).unwrap();
        assert_eq!(rv, buffer.len());

        // write a packet that would overflow the pipe, but there is less than PIPE_BUF space
        // remaining in the pipe
        let buffer = vec![0u8; libc::PIPE_BUF * 4];
        let rv = nix::unistd::write(write_fd, &buffer).err().unwrap();

        // no partial packet is written
        assert_eq!(rv, nix::errno::Errno::EWOULDBLOCK);

        Ok(())
    })
}

fn test_close_during_blocking_read() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe2(fds.as_mut_ptr(), 0) } }, &[])?;

    assert!(fds[0] > 0, "fds[0] not set");
    assert!(fds[1] > 0, "fds[1] not set");

    let (read_fd, write_fd) = (fds[0], fds[1]);

    let thread_handle = std::thread::spawn(move || {
        // 2. wait for the first read() to start
        std::thread::sleep(Duration::from_secs(1));

        // 3. close the reader while the first read() is blocked
        nix::unistd::close(read_fd).unwrap();

        // 4. wake the reader by writing
        assert_eq!(nix::unistd::write(write_fd, &[1, 2, 3]), Ok(3));

        // 6. wait for the second read() to complete
        std::thread::sleep(Duration::from_secs(1));

        // 7. after the read() syscall completes the read end should be closed, so we can't write
        // anymore
        assert_eq!(
            nix::unistd::write(write_fd, &[1, 2, 3]),
            Err(nix::errno::Errno::EPIPE)
        );
        nix::unistd::close(write_fd).unwrap();
    });

    // 1. the first read will block until there are bytes to read
    let mut buf = vec![0u8; 10];
    assert_eq!(nix::unistd::read(read_fd, &mut buf), Ok(3));

    // 5. after returning from the first read, the read fd should be closed
    assert_eq!(
        nix::unistd::read(read_fd, &mut buf),
        Err(nix::errno::Errno::EBADF)
    );

    thread_handle.join().unwrap();

    Ok(())
}

fn test_close_during_blocking_write() -> Result<(), String> {
    let mut fds = [0 as libc::c_int; 2];
    test_utils::check_system_call!(|| { unsafe { libc::pipe2(fds.as_mut_ptr(), 0) } }, &[])?;

    assert!(fds[0] > 0, "fds[0] not set");
    assert!(fds[1] > 0, "fds[1] not set");

    let (read_fd, write_fd) = (fds[0], fds[1]);

    let thread_handle = std::thread::spawn(move || {
        // 2. wait for writes to block
        std::thread::sleep(Duration::from_secs(1));

        // 3. close the writer while the write() is blocked
        nix::unistd::close(write_fd).unwrap();

        // 4. wake the writer by reading
        // on linux, must read an amount greater than one page in order to unblock writer:
        // https://stackoverflow.com/a/29233953
        let mut buf = vec![0u8; 5000];
        assert_eq!(nix::unistd::read(read_fd, &mut buf), Ok(5000));

        // 6. wait for the blocked write() to complete
        std::thread::sleep(Duration::from_secs(1));

        // 7. after the write() syscall completes the write end should be closed, so we can read
        // bytes until the buffer is empty, then will read EOF
        loop {
            match nix::unistd::read(read_fd, &mut buf) {
                Ok(0) => break,
                Ok(x) => assert!(x > 0),
                Err(e) => panic!("Unexpected error {}", e),
            }
        }
        nix::unistd::close(read_fd).unwrap();
    });

    loop {
        // 1. write repeatedly until it blocks
        match nix::unistd::write(write_fd, &[0u8; 789]) {
            // linux will only write in 789 byte chuncks, but shadow might write fewer
            Ok(n) => assert!(n > 0 && n <= 789),
            Err(nix::errno::Errno::EBADF) => break,
            Err(e) => panic!("Unexpected error {}", e),
        }
    }

    // 5. write should fail since the write end is closed
    assert_eq!(
        nix::unistd::write(write_fd, &[0u8; 789]),
        Err(nix::errno::Errno::EBADF)
    );

    thread_handle.join().unwrap();

    Ok(())
}
