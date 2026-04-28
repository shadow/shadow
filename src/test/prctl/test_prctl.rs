use linux_api::prctl::ArchPrctlOp;
use nix::poll::{PollFd, PollFlags, poll};
use nix::sys::signal::{SaFlags, SigAction, SigHandler, SigSet, Signal as NixSignal};
use rustix::fd::AsRawFd;
use rustix::io::{read, write};
use rustix::pipe::pipe;
use std::sync::atomic::{AtomicI32, Ordering};
use test_utils::TestEnvironment as TestEnv;
use test_utils::{assert_with_errno, set};

static PARENT_DEATH_SIGNAL_WRITE_FD: AtomicI32 = AtomicI32::new(-1);
static PARENT_DEATH_SIGNAL_NUMBER: AtomicI32 = AtomicI32::new(0);
static PARENT_DEATH_SIGNAL_SENDER: AtomicI32 = AtomicI32::new(0);

extern "C" fn parent_death_signal_handler(
    signal: libc::c_int,
    info: *mut libc::siginfo_t,
    _ctx: *mut libc::c_void,
) {
    PARENT_DEATH_SIGNAL_NUMBER.store(signal, Ordering::SeqCst);

    let sender = if info.is_null() {
        0
    } else {
        unsafe { (*info).si_pid() }
    };
    PARENT_DEATH_SIGNAL_SENDER.store(sender, Ordering::SeqCst);

    let fd = PARENT_DEATH_SIGNAL_WRITE_FD.load(Ordering::SeqCst);
    if fd >= 0 {
        let byte = [1u8];
        let _ = unsafe { libc::write(fd, byte.as_ptr().cast(), byte.len()) };
    }
}

fn wait_for_parent_death_signal(fd: libc::c_int) -> Result<(), String> {
    let mut poll_fds = [PollFd::new(fd, PollFlags::POLLIN)];

    loop {
        match poll(&mut poll_fds, 2000) {
            Ok(0) => return Err("Timed out waiting for parent-death signal".into()),
            Ok(_) => {
                let events = poll_fds[0].revents().unwrap_or(PollFlags::empty());
                if events.contains(PollFlags::POLLIN) {
                    return Ok(());
                }

                return Err(format!(
                    "Unexpected poll events while waiting for signal: {events:?}"
                ));
            }
            Err(nix::errno::Errno::EINTR) => continue,
            Err(err) => return Err(format!("poll failed while waiting for signal: {err}")),
        }
    }
}

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
        test_utils::ShadowTest::new(
            "test_zero_option",
            test_zero_option,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_dumpable",
            test_dumpable,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_tid_addr",
            test_tid_addr,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_parent_death_signal",
            test_parent_death_signal,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new(
            "test_parent_death_signal_delivery",
            test_parent_death_signal_delivery,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
        test_utils::ShadowTest::new("test_name", test_name, set![TestEnv::Libc, TestEnv::Shadow]),
        test_utils::ShadowTest::new(
            "test_trap_cpuid",
            test_trap_cpuid,
            set![TestEnv::Libc, TestEnv::Shadow],
        ),
    ];

    tests
}

fn test_zero_option() -> Result<(), String> {
    assert_eq!(-1, unsafe { libc::prctl(0) });
    assert_eq!(libc::EINVAL, test_utils::get_errno());

    Ok(())
}

fn test_dumpable() -> Result<(), String> {
    // In kernels up to and including 2.6.12, arg2 must be either 0 (SUID_DUMP_DISABLE, process is
    // not dumpable) or 1 (SUID_DUMP_USER, process is dumpable).
    const SUID_DUMP_DISABLE: u64 = 0;
    const SUID_DUMP_USER: u64 = 1;

    // should initially be enabled
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_DUMPABLE) } == SUID_DUMP_USER as i32);

    // set as disabled
    assert_with_errno!(unsafe { libc::prctl(libc::PR_SET_DUMPABLE, SUID_DUMP_DISABLE) } == 0);

    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_DUMPABLE) } == SUID_DUMP_DISABLE as i32);

    Ok(())
}

fn test_tid_addr() -> Result<(), String> {
    let mut addr: *mut libc::pid_t = std::ptr::null_mut();
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_TID_ADDRESS, &mut addr) } == 0);

    // it seems to be null in shadow and non-null outside of shadow

    // check that the pointer is readable
    if !addr.is_null() {
        // printing the value so that DCE doesn't optimize out the read (`read_volatile` and
        // `black_box` aren't enough)
        println!("{}", unsafe { *addr });
    }

    Ok(())
}

fn test_parent_death_signal() -> Result<(), String> {
    let mut signal = -1;

    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_PDEATHSIG, &mut signal) } == 0);
    assert_eq!(signal, 0);

    assert_with_errno!(unsafe { libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGTERM) } == 0);
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_PDEATHSIG, &mut signal) } == 0);
    assert_eq!(signal, libc::SIGTERM);

    assert_with_errno!(unsafe { libc::prctl(libc::PR_SET_PDEATHSIG, 0) } == 0);
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_PDEATHSIG, &mut signal) } == 0);
    assert_eq!(signal, 0);

    Ok(())
}

fn test_parent_death_signal_delivery() -> Result<(), String> {
    let (ready_reader, ready_writer) = pipe().unwrap();
    let (result_reader, result_writer) = pipe().unwrap();

    let supervisor_pid = unsafe { libc::fork() };
    assert_with_errno!(supervisor_pid >= 0);

    if supervisor_pid == 0 {
        let worker_pid = unsafe { libc::fork() };
        if worker_pid < 0 {
            unsafe { libc::_exit(10) };
        }

        if worker_pid == 0 {
            let expected_parent_pid = unsafe { libc::getppid() };
            let (signal_reader, signal_writer) = pipe().unwrap();
            PARENT_DEATH_SIGNAL_WRITE_FD.store(signal_writer.as_raw_fd(), Ordering::SeqCst);
            PARENT_DEATH_SIGNAL_NUMBER.store(0, Ordering::SeqCst);
            PARENT_DEATH_SIGNAL_SENDER.store(0, Ordering::SeqCst);

            let action = SigAction::new(
                SigHandler::SigAction(parent_death_signal_handler),
                SaFlags::SA_SIGINFO,
                SigSet::empty(),
            );
            assert!(unsafe { nix::sys::signal::sigaction(NixSignal::SIGUSR1, &action) }.is_ok());

            assert_eq!(
                unsafe { libc::prctl(libc::PR_SET_PDEATHSIG, libc::SIGUSR1) },
                0
            );

            let mut configured_signal = -1;
            assert_eq!(
                unsafe { libc::prctl(libc::PR_GET_PDEATHSIG, &mut configured_signal) },
                0
            );
            assert_eq!(configured_signal, libc::SIGUSR1);

            assert_eq!(write(&ready_writer, &[1]), Ok(1));
            drop(ready_writer);

            let status = match wait_for_parent_death_signal(signal_reader.as_raw_fd()) {
                Ok(()) => {
                    let mut byte = [0];
                    let read_result = read(&signal_reader, &mut byte);
                    if read_result == Ok(1)
                        && PARENT_DEATH_SIGNAL_NUMBER.load(Ordering::SeqCst) == libc::SIGUSR1
                        && PARENT_DEATH_SIGNAL_SENDER.load(Ordering::SeqCst) == expected_parent_pid
                    {
                        0
                    } else {
                        1
                    }
                }
                Err(err) => {
                    eprintln!("{err}");
                    1
                }
            };

            PARENT_DEATH_SIGNAL_WRITE_FD.store(-1, Ordering::SeqCst);

            let _ = write(&result_writer, &[status]);
            unsafe { libc::_exit(status.into()) };
        }

        drop(ready_writer);
        drop(result_writer);

        let mut ready = [0];
        match read(&ready_reader, &mut ready) {
            Ok(1) if ready[0] == 1 => unsafe { libc::_exit(0) },
            _ => unsafe { libc::_exit(11) },
        }
    }

    drop(ready_reader);
    drop(ready_writer);
    drop(result_writer);

    let mut wait_status = 0;
    assert_with_errno!(
        unsafe { libc::waitpid(supervisor_pid, &mut wait_status, 0) } == supervisor_pid
    );
    assert!(libc::WIFEXITED(wait_status));
    assert_eq!(libc::WEXITSTATUS(wait_status), 0);

    let mut result = [255];
    assert_eq!(read(&result_reader, &mut result), Ok(1));
    assert_eq!(result[0], 0);

    Ok(())
}

fn test_trap_cpuid() -> Result<(), String> {
    // It should *look like* cpuid is permitted, whether or not we're running under shadow.
    let res = unsafe { linux_api::prctl::arch_prctl(ArchPrctlOp::ARCH_GET_CPUID, 0) };
    assert_eq!(res, Ok(1));

    // If we're running under shadow, trying to trap cpuid should fail.
    if test_utils::running_in_shadow() {
        let res = unsafe { linux_api::prctl::arch_prctl(ArchPrctlOp::ARCH_SET_CPUID, 0) };
        assert_eq!(res, Err(linux_api::errno::Errno::ENODEV));
    }

    Ok(())
}

fn test_name() -> Result<(), String> {
    let mut buffer = [0u8; 16];
    assert_with_errno!(unsafe { libc::prctl(libc::PR_GET_NAME, &mut buffer) } == 0);

    // make sure it's null terminated
    assert_eq!(buffer[buffer.len() - 1], 0);

    Ok(())
}
