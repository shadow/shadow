use std::arch::asm;
use std::error::Error;
use std::os::unix::io::RawFd;
use std::sync::mpsc::channel;
use std::sync::Arc;
use std::time::Duration;

use nix::errno::Errno;
use nix::sys::signal;
use nix::sys::signal::Signal;
use nix::unistd;
use once_cell::sync::OnceCell;
use rustix::thread::NanosleepRelativeResult;
use rustix::time::Timespec;
use signal_hook::low_level::channel::Channel as SignalSafeChannel;
use test_utils::running_in_shadow;
use test_utils::set;
use test_utils::setitimer;
use test_utils::ShadowTest;
use test_utils::TestEnvironment as TestEnv;

const SS_AUTODISARM: libc::c_int = 1 << 31;

fn sigaltstack(new: Option<&libc::stack_t>, old: Option<&mut libc::stack_t>) -> Result<(), Errno> {
    let new = match new {
        Some(r) => std::ptr::from_ref(r),
        None => std::ptr::null(),
    };
    let old = match old {
        Some(r) => std::ptr::from_mut(r),
        None => std::ptr::null_mut(),
    };
    Errno::result(unsafe { libc::sigaltstack(new, old) })?;
    Ok(())
}

// Record of having received a signal.
#[derive(Debug, Eq, PartialEq)]
struct Record {
    signal: i32,
    pid: unistd::Pid,
    tid: unistd::Pid,
    info: Option<libc::siginfo_t>,
}

// Global channel to be written from the signal handler.  We use
// signal_hook::low_level::channel::Channel here, which is explicitly designed
// to be async-signal-safe.
fn signal_channel() -> &'static SignalSafeChannel<Record> {
    static INSTANCE: OnceCell<SignalSafeChannel<Record>> = OnceCell::new();
    INSTANCE.get_or_init(SignalSafeChannel::new)
}

// Signal handler used throughout. Tests read from `signal_channel` to validate
// properties of the received signal.
extern "C" fn signal_action(signal: i32, info: *mut libc::siginfo_t, _ctx: *mut std::ffi::c_void) {
    // Try to use only async-signal-safe functions. See signal-safety(7).
    //
    // Following this strictly is *very* restrictive, since most apis don't
    // bother to document/guarantee signal safety. Definitely avoid anything
    // known to be non-reentrant, though, including heap allocation.

    let record = Record {
        signal,
        pid: unistd::getpid(),
        tid: unistd::gettid(),
        info: unsafe { info.as_ref().cloned() },
    };
    signal_channel().send(record);
}

// Legacy/simple style handler.
extern "C" fn signal_handler(sig: i32) {
    signal_action(sig, std::ptr::null_mut(), std::ptr::null_mut());
}

fn catchable_signals() -> Box<dyn Iterator<Item = Signal>> {
    Box::new(Signal::iterator().filter(|s| match s {
        // Can't be caught, as per `sigaction(2)`.
        Signal::SIGKILL | Signal::SIGSTOP => false,
        _ => true,
    }))
}

fn tkill(tid: unistd::Pid, signal: Signal) -> Result<(), Errno> {
    Errno::result(unsafe { libc::syscall(libc::SYS_tkill, tid, signal) })?;
    Ok(())
}

fn tgkill(pid: unistd::Pid, tid: unistd::Pid, signal: Signal) -> Result<(), Errno> {
    Errno::result(unsafe { libc::syscall(libc::SYS_tgkill, pid, tid, signal) })?;
    Ok(())
}

// Not exposed in Rust's libc crate.
#[derive(Debug, Eq, PartialEq, Copy, Clone)]
#[allow(non_camel_case_types)]
enum SignalCode {
    SI_TKILL = -6,
    SI_USER = 0,
}

// Tests basic signal delivery to self.
fn test_raise(
    raise_fn: &dyn Fn(Signal),
    expected_code: Option<SignalCode>,
) -> Result<(), Box<dyn Error>> {
    for handler in &[
        signal::SigHandler::SigIgn,
        signal::SigHandler::SigAction(signal_action),
        signal::SigHandler::Handler(signal_handler),
    ] {
        for signal in catchable_signals() {
            println!("Signal {}", signal);
            unsafe {
                signal::sigaction(
                    signal,
                    &signal::SigAction::new(
                        *handler,
                        signal::SaFlags::empty(),
                        signal::SigSet::empty(),
                    ),
                )
                .unwrap()
            };

            raise_fn(signal);

            if handler == &signal::SigHandler::SigIgn {
                // Should be ignored.
                assert_eq!(signal_channel().recv(), None);
                continue;
            }

            // Exactly one signal should have been delivered, synchronously.
            let record = signal_channel().recv().unwrap();
            assert_eq!(signal_channel().recv(), None);

            assert_eq!(record.pid, unistd::getpid());
            assert_eq!(record.tid, unistd::gettid());
            assert_eq!(Signal::try_from(record.signal).unwrap(), signal);

            match handler {
                signal::SigHandler::SigAction(_) => {
                    let info = record.info.unwrap();
                    assert_eq!(Signal::try_from(info.si_signo).unwrap(), signal);
                    if let Some(expected_code) = expected_code {
                        assert_eq!(info.si_code, expected_code as i32);
                    }
                    assert_eq!(
                        unistd::Pid::from_raw(unsafe { info.si_pid() }),
                        unistd::getpid()
                    );
                }
                signal::SigHandler::Handler(_) => {
                    assert_eq!(record.info, None);
                }
                _ => unreachable!(),
            };

            unsafe {
                signal::sigaction(
                    signal,
                    &signal::SigAction::new(
                        signal::SigHandler::SigDfl,
                        signal::SaFlags::empty(),
                        signal::SigSet::empty(),
                    ),
                )
                .unwrap()
            };
        }
    }
    Ok(())
}

// Test process-directed signals with multiple threads.
fn test_process_pending_multithreaded() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;

    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::Handler(signal_handler),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let run = Arc::new(std::sync::atomic::AtomicBool::new(true));
    let mut threads = Vec::new();
    for _ in 0..10 {
        let run = run.clone();
        threads.push(std::thread::spawn(move || {
            while run.load(std::sync::atomic::Ordering::Relaxed) {
                std::thread::sleep(Duration::from_millis(1));
            }
        }));
    }

    signal::kill(unistd::getpid(), signal).unwrap();

    // Signal should be delivered exactly once, to one of the threads.  When
    // there are other threads with the signal unblocked, POSIX.1 doesn't
    // guarantee that the signal will be delivered to the current thread nor
    // that it will delivered before `kill` returns.
    let mut record = signal_channel().recv();
    while record.is_none() {
        std::thread::sleep(Duration::from_millis(1));
        record = signal_channel().recv();
    }
    let record = record.unwrap();
    assert_eq!(record.pid, unistd::getpid());
    // TODO: check that tid is one of our threads?
    // assert_ne!(record.tid, unistd::gettid());
    assert_eq!(Signal::try_from(record.signal).unwrap(), signal);
    assert_eq!(signal_channel().recv(), None);

    // Shut down
    run.store(false, std::sync::atomic::Ordering::Relaxed);
    for thread in threads {
        thread.join().unwrap();
    }

    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigDfl,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };
    Ok(())
}

fn test_sigprocmask() -> Result<(), Box<dyn Error>> {
    for signal in catchable_signals() {
        println!("{}", signal);
        unsafe {
            signal::sigaction(
                signal,
                &signal::SigAction::new(
                    signal::SigHandler::Handler(signal_handler),
                    signal::SaFlags::empty(),
                    signal::SigSet::empty(),
                ),
            )
            .unwrap()
        };

        // Block the signal
        let mut sigset_to_block = signal::SigSet::empty();
        sigset_to_block.add(signal);
        signal::sigprocmask(signal::SigmaskHow::SIG_BLOCK, Some(&sigset_to_block), None).unwrap();

        // Validate that the signal is in the returned mask.
        let mut current_sigset = signal::SigSet::empty();
        signal::sigprocmask(
            signal::SigmaskHow::SIG_BLOCK,
            None,
            Some(&mut current_sigset),
        )
        .unwrap();
        assert!(current_sigset.contains(signal));

        // Raise multiple times; won't be delivered yet because it's masked,
        // and subsequent signals aren't queued.
        signal::raise(signal).unwrap();
        signal::raise(signal).unwrap();
        signal::raise(signal).unwrap();

        // Should be nothing delivered yet, since the signal is unblocked.
        assert_eq!(signal_channel().recv(), None);

        // Unblock. The pending signal should be delivered synchronously.
        signal::sigprocmask(
            signal::SigmaskHow::SIG_UNBLOCK,
            Some(&sigset_to_block),
            None,
        )
        .unwrap();

        // Exactly one signal should have been delivered, synchronously.
        let record = signal_channel().recv().unwrap();
        assert_eq!(record.pid, unistd::getpid());
        assert_eq!(record.tid, unistd::gettid());
        assert_eq!(Signal::try_from(record.signal).unwrap(), signal);
        assert_eq!(signal_channel().recv(), None);

        unsafe {
            signal::sigaction(
                signal,
                &signal::SigAction::new(
                    signal::SigHandler::SigDfl,
                    signal::SaFlags::empty(),
                    signal::SigSet::empty(),
                ),
            )
            .unwrap()
        };
    }
    Ok(())
}

fn test_send_to_thread_and_process() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::Handler(signal_handler),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    // Block the signal
    let mut sigset_to_block = signal::SigSet::empty();
    sigset_to_block.add(signal);
    signal::sigprocmask(signal::SigmaskHow::SIG_BLOCK, Some(&sigset_to_block), None).unwrap();

    // Raising should set it pending at the process level.
    signal::kill(unistd::getpid(), signal).unwrap();

    // tkill should *also* set it pending at the thread level.
    tkill(unistd::gettid(), signal).unwrap();

    // Should be nothing delivered yet, since the signal is unblocked.
    assert_eq!(signal_channel().recv(), None);

    // Unblock. The pending signal should be delivered synchronously.
    signal::sigprocmask(
        signal::SigmaskHow::SIG_UNBLOCK,
        Some(&sigset_to_block),
        None,
    )
    .unwrap();

    // Should be delivered exactly twice.
    signal_channel().recv().unwrap();
    signal_channel().recv().unwrap();
    assert_eq!(signal_channel().recv(), None);

    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigDfl,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };
    Ok(())
}

struct BlockedThread {
    handle: std::thread::JoinHandle<Result<usize, Errno>>,
    write_fd: RawFd,
    tid: unistd::Pid,
}

extern "C" fn nop_signal_handler(_sig: i32) {}

impl BlockedThread {
    pub fn new() -> Self {
        let (read_fd, write_fd) = unistd::pipe().unwrap();
        let (tid_sender, tid_receiver) = channel();
        let handle = std::thread::spawn(move || {
            tid_sender.send(unistd::gettid()).unwrap();
            let mut buf = [0; 100];
            unistd::read(read_fd, &mut buf)
        });
        let tid = tid_receiver.recv().unwrap();
        // Wait until the thread is blocked in `read` (hopefully).  Ideally we'd
        // use some syscall that atomically blocks the current thread and
        // changes some observable state in other threads, so that we could wait
        // until we knew the thread was blocked. I don't know of any such
        // syscall that other threads can check without side effects.
        std::thread::sleep(Duration::from_millis(10));
        Self {
            handle,
            write_fd,
            tid,
        }
    }
}

fn test_handled_tkill_interrupts_syscall() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();
    tkill(blocked_thread.tid, signal).unwrap();
    assert_eq!(blocked_thread.handle.join().unwrap(), Err(Errno::EINTR));

    Ok(())
}

fn test_ignored_tkill_doesnt_interrupt_syscall() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigIgn,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();
    tkill(blocked_thread.tid, signal).unwrap();
    // Give some time for the thread to (incorrectly) interrupt.
    std::thread::sleep(Duration::from_millis(10));
    // Write so that the thread can finish.
    unistd::write(blocked_thread.write_fd, &[0]).unwrap();
    assert_eq!(blocked_thread.handle.join().unwrap(), Ok(1));

    Ok(())
}

fn test_default_ignored_tkill_doesnt_interrupt_syscall() -> Result<(), Box<dyn Error>> {
    // Default action of this signal is to ignore.
    let signal = Signal::SIGURG;

    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                // Explicity set to default action.
                signal::SigHandler::SigDfl,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();
    tkill(blocked_thread.tid, signal).unwrap();
    // Give some time for the thread to (incorrectly) interrupt.
    std::thread::sleep(Duration::from_millis(10));
    // Write so that the thread can finish.
    unistd::write(blocked_thread.write_fd, &[0]).unwrap();
    assert_eq!(blocked_thread.handle.join().unwrap(), Ok(1));

    Ok(())
}
fn test_handled_kill_interrupts_syscall() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();

    // Block the signal in the current thread, so that it'll interrupt the other.
    let mut sigset_to_block = signal::SigSet::empty();
    sigset_to_block.add(signal);
    signal::sigprocmask(signal::SigmaskHow::SIG_BLOCK, Some(&sigset_to_block), None).unwrap();

    // Send a *process* directed signal.
    signal::kill(unistd::getpid(), signal).unwrap();
    assert_eq!(blocked_thread.handle.join().unwrap(), Err(Errno::EINTR));

    signal::sigprocmask(
        signal::SigmaskHow::SIG_UNBLOCK,
        Some(&sigset_to_block),
        None,
    )
    .unwrap();

    Ok(())
}

fn test_restart() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::SA_RESTART,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();

    // Raise a signal. Since we used SA_RESTART and there was nothing to read
    // yet, the target thread's `read` operation on the pipe should be
    // transparently restarted, leaving it blocked again.
    tkill(blocked_thread.tid, signal).unwrap();

    // Give child thread a chance to run.
    std::thread::sleep(Duration::from_millis(10));

    // Now write so that the thread can finish normally.
    unistd::write(blocked_thread.write_fd, &[0]).unwrap();
    assert_eq!(blocked_thread.handle.join().unwrap(), Ok(1));

    Ok(())
}

fn test_restart_all() -> Result<(), Box<dyn Error>> {
    // Install 2 signal handlers, both with SA_RESTART.
    let signal1 = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal1,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::SA_RESTART,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };
    let signal2 = Signal::SIGUSR2;
    unsafe {
        signal::sigaction(
            signal2,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::SA_RESTART,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();

    // Raise both signals. Since they both use SA_RESTART and there was nothing
    // to read yet, the target thread's `read` operation on the pipe should be
    // transparently restarted, leaving it blocked again.
    tkill(blocked_thread.tid, signal1).unwrap();
    tkill(blocked_thread.tid, signal2).unwrap();

    // Give child thread a chance to run.
    std::thread::sleep(Duration::from_millis(10));

    // Now write so that the thread can finish normally.
    unistd::write(blocked_thread.write_fd, &[0]).unwrap();
    assert_eq!(blocked_thread.handle.join().unwrap(), Ok(1));

    Ok(())
}

fn test_restart_first() -> Result<(), Box<dyn Error>> {
    // Install 2 signal handlers, the lower-numbered one with SA_RESTART.
    let signal1 = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal1,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::SA_RESTART,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };
    let signal2 = Signal::SIGUSR2;
    unsafe {
        signal::sigaction(
            signal2,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();

    // Raise both signals.

    tkill(blocked_thread.tid, signal1).unwrap();
    tkill(blocked_thread.tid, signal2).unwrap();

    // In Linux we can't predict in what order the signals will be delivered, or
    // how long after the 1st signal will the 2nd be delivered.
    //
    // In Shadow, both signals will atomically be pending when the thread
    // next runs, and the lower numbered signal will be handled first. This
    // is SIGUSR1, which has SA_RESTART set. Therefore:
    //  * The signal with SA_RESTART is delivered, handled, and the syscall
    //    restarted, blocking again.
    //  * The signal without SA_RESTART is delivered, handled, and the syscall
    //    *not* restarted, causing the syscall to return EINTR.
    // This sequence is also valid, but not guaranteed, in Linux.
    assert_eq!(blocked_thread.handle.join().unwrap(), Err(Errno::EINTR));

    Ok(())
}

fn test_restart_second() -> Result<(), Box<dyn Error>> {
    // Install 2 signal handlers, the higher-numbered one with SA_RESTART.
    let signal1 = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal1,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };
    let signal2 = Signal::SIGUSR2;
    unsafe {
        signal::sigaction(
            signal2,
            &signal::SigAction::new(
                signal::SigHandler::Handler(nop_signal_handler),
                signal::SaFlags::SA_RESTART,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let blocked_thread = BlockedThread::new();

    // Raise both signals.

    tkill(blocked_thread.tid, signal1).unwrap();
    tkill(blocked_thread.tid, signal2).unwrap();

    // In Linux we can't predict in what order the signals will be delivered, or
    // how long after the 1st signal will the 2nd be delivered.
    //
    // In Shadow, both signals will atomically be pending when the thread
    // next runs, and the lower numbered signal will be handled first. This
    // is SIGUSR1, which doesn't have SA_RESTART set. Therefore:
    //  * SIGUSR1, without SA_RESTART is delivered, handled, and the syscall
    //    is set to return EINTR.
    //  * Before the next instruction after the syscall is allowed to executed,
    //    SIGUSR2 is delivered and handled. While it has SA_RESTART set,
    //    it doesn't matter since the first signal already caused the syscall to
    //    irreversibly fail.
    //
    // This sequence is also valid, but not guaranteed, in Linux.
    assert_eq!(blocked_thread.handle.join().unwrap(), Err(Errno::EINTR));

    Ok(())
}

// Record of having received a signal.
#[derive(Debug)]
struct SigaltstackRecord {
    rsp: usize,
    altstack: libc::stack_t,
}

// Normally the void* inside libc::stack_t prevents SigaltstackRecord from being
// `Send`. We never dereference the pointer, though.
unsafe impl Send for SigaltstackRecord {}

// Global channel to be written from the signal handler.  We use
// signal_hook::low_level::channel::Channel here, which is explicitly designed
// to be async-signal-safe.
fn sigaltstack_channel() -> &'static SignalSafeChannel<SigaltstackRecord> {
    static INSTANCE: OnceCell<SignalSafeChannel<SigaltstackRecord>> = OnceCell::new();
    INSTANCE.get_or_init(SignalSafeChannel::new)
}

extern "C" fn sigaltstack_action(
    _signal: i32,
    _info: *mut libc::siginfo_t,
    _ctx: *mut std::ffi::c_void,
) {
    // Try to use only async-signal-safe functions. See signal-safety(7).
    //
    // Following this strictly is *very* restrictive, since most apis don't
    // bother to document/guarantee signal safety. Definitely avoid anything
    // known to be non-reentrant, though, including heap allocation.

    // Get address of a stack-allocated value, which we use to detect which
    // stack the handler is running on. Using assembly to get `rsp` directly
    // might be a little nicer, but is currently unstable in Rust.
    let stack_var = 0u64;
    let rsp = std::ptr::from_ref(&stack_var) as usize;

    let mut altstack = libc::stack_t {
        ss_sp: std::ptr::null_mut(),
        ss_flags: 0,
        ss_size: 0,
    };
    sigaltstack(None, Some(&mut altstack)).unwrap();

    let record = SigaltstackRecord { rsp, altstack };
    sigaltstack_channel().send(record);
}

fn test_sigaltstack_unconfigured() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(sigaltstack_action),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    let altstack = libc::stack_t {
        ss_sp: std::ptr::null_mut(),
        ss_flags: libc::SS_DISABLE,
        ss_size: 0,
    };
    sigaltstack(Some(&altstack), None)?;

    // No altstack configured.
    signal::raise(signal).unwrap();
    let record = sigaltstack_channel().recv().unwrap();
    assert_eq!(record.altstack, altstack);

    Ok(())
}

fn test_sigaltstack_configured_but_unused() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(sigaltstack_action),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    // Configure an altstack.
    const STACK_SZ: usize = 1 << 20;
    let mut stack_space = Box::new([0u8; STACK_SZ]);
    let stack_range_start = std::ptr::from_ref(&stack_space[0]) as usize;
    let stack_range = stack_range_start..(stack_range_start + STACK_SZ);
    let altstack = libc::stack_t {
        ss_sp: std::ptr::from_mut(&mut stack_space[0]) as *mut libc::c_void,
        ss_flags: 0,
        ss_size: STACK_SZ,
    };
    sigaltstack(Some(&altstack), None)?;

    // Should see the configured altstack in the handler.
    signal::raise(signal).unwrap();
    let record = sigaltstack_channel().recv().unwrap();
    assert_eq!(record.altstack, altstack);

    // Handler *shouldn't* be running on the altstack, since it was registered
    // without SA_ONSTACK.
    assert!(!stack_range.contains(&record.rsp));

    Ok(())
}

fn test_sigaltstack_used() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(sigaltstack_action),
                signal::SaFlags::SA_ONSTACK,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    // Configure an altstack.
    const STACK_SZ: usize = 1 << 20;
    let mut stack_space = Box::new([0u8; STACK_SZ]);
    let stack_range_start = std::ptr::from_ref(&stack_space[0]) as usize;
    let stack_range = stack_range_start..(stack_range_start + STACK_SZ);
    let altstack = libc::stack_t {
        ss_sp: std::ptr::from_mut(&mut stack_space[0]) as *mut libc::c_void,
        ss_flags: 0,
        ss_size: STACK_SZ,
    };
    sigaltstack(Some(&altstack), None)?;

    // Should see the configured altstack in the handler.
    signal::raise(signal).unwrap();
    let record = sigaltstack_channel().recv().unwrap();

    // Handler *should* be running on the altstack
    let mut expected_stack = altstack;
    expected_stack.ss_flags |= libc::SS_ONSTACK;
    assert_eq!(record.altstack, expected_stack);
    assert!(stack_range.contains(&record.rsp));

    Ok(())
}

fn test_sigaltstack_autodisarm() -> Result<(), Box<dyn Error>> {
    let signal = Signal::SIGUSR1;
    unsafe {
        signal::sigaction(
            signal,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(sigaltstack_action),
                signal::SaFlags::SA_ONSTACK,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    // Configure an altstack.
    const STACK_SZ: usize = 1 << 20;
    let mut stack_space = Box::new([0u8; STACK_SZ]);
    let stack_range_start = std::ptr::from_ref(&stack_space[0]) as usize;
    let stack_range = stack_range_start..(stack_range_start + STACK_SZ);
    let altstack = libc::stack_t {
        ss_sp: std::ptr::from_mut(&mut stack_space[0]) as *mut libc::c_void,
        ss_flags: SS_AUTODISARM,
        ss_size: STACK_SZ,
    };
    sigaltstack(Some(&altstack), None)?;

    // Should see the configured altstack in the handler.
    signal::raise(signal).unwrap();
    let record = sigaltstack_channel().recv().unwrap();

    // Handler should be running on the altstack.
    assert!(stack_range.contains(&record.rsp));

    // The altstack config should be disarmed while the handler was running.
    assert_eq!(
        record.altstack,
        libc::stack_t {
            ss_sp: std::ptr::null_mut(),
            ss_flags: libc::SS_DISABLE,
            ss_size: 0
        }
    );

    // The altstack config should be restored now that the handler has returned.
    let mut final_altstack = libc::stack_t {
        ss_sp: std::ptr::null_mut(),
        ss_flags: 0,
        ss_size: 0,
    };
    sigaltstack(None, Some(&mut final_altstack)).unwrap();
    assert_eq!(final_altstack, altstack);

    Ok(())
}

static GLOBAL_STATIC: u32 = 0xdeadbeef;
extern "C" fn change_rax_from_null_to_global_static(
    signal: i32,
    info: *mut libc::siginfo_t,
    voidctx: *mut std::ffi::c_void,
) {
    assert_eq!(signal, signal::SIGSEGV as i32);

    let info = unsafe { info.as_ref().unwrap() };
    // Not exposed in libc crate
    const SEGV_MAPERR: i32 = 1;
    assert_eq!(info.si_code, SEGV_MAPERR);
    assert!(unsafe { info.si_addr().is_null() });

    let ctx = voidctx as *mut libc::ucontext_t;
    let ctx = unsafe { ctx.as_mut().unwrap() };
    ctx.uc_mcontext.gregs[libc::REG_RAX as usize] = std::ptr::from_ref(&GLOBAL_STATIC) as i64;
}

fn test_synchronous_sigsegv() -> Result<(), Box<dyn Error>> {
    // Ensure SIGSEGV isn't blocked.
    let mut sigset = signal::SigSet::empty();
    sigset.add(signal::SIGSEGV);
    signal::sigprocmask(signal::SigmaskHow::SIG_UNBLOCK, Some(&sigset), None).unwrap();

    // Install our SIGSEGV handler, which is going to mutate registers in the
    // caller to fix the null pointer dereference below.
    // This may seem esoteric, but this is sometimes done to implement custom
    // memory management, or to implement higher level error handling.
    // e.g. in OpenJDK SIGSEGVs in managed code are transformed into NullPointerException.
    // https://github.com/shadow/shadow/issues/2091#issuecomment-1111374729
    unsafe {
        signal::sigaction(
            signal::SIGSEGV,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(change_rax_from_null_to_global_static),
                // Override the default behavior of blocking the current signal,
                // since generating a SIGSEGV while SIGSEGV is blocked is
                // undefined behavior.
                signal::SaFlags::SA_NODEFER,
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    // Dereference a NULL pointer from rax. The SIGSEGV handler will rewrite rax
    // to a valid pointer.
    let mut addr = 0u64;
    let mut val: u32;
    unsafe {
        asm!("mov {val:e}, [rax]", val = out(reg) val, inout("rax") addr);
    }
    assert_eq!(addr, std::ptr::from_ref(&GLOBAL_STATIC) as u64);
    assert_eq!(val, GLOBAL_STATIC);

    // Restore default action to avoid surprising behavior in the case of an
    // unexpected SIGSEGV later.
    unsafe {
        signal::sigaction(
            signal::SIGSEGV,
            &signal::SigAction::new(
                signal::SigHandler::SigDfl,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    Ok(())
}

static mut LAST_ERROR_SIGNAL: i32 = 0;
static mut RECOVERY_POINT: *mut libc::ucontext_t = std::ptr::null_mut();

extern "C" fn recover_from_hardware_error(
    signal: i32,
    info: *mut libc::siginfo_t,
    voidctx: *mut std::ffi::c_void,
) {
    assert!(!info.is_null());
    assert!(!voidctx.is_null());
    // Rough approximation of how some language runtimes recover from such
    // signals; generally it will be turned into a language exception of some
    // kind at the recovery point.
    unsafe { LAST_ERROR_SIGNAL = signal };
    unsafe { libc::setcontext(RECOVERY_POINT) };
    panic!("Unreachable");
}

fn test_hardware_error_signal<F: FnOnce()>(
    sig: signal::Signal,
    setcontext_and_raise_err: F,
) -> Result<(), Box<dyn Error>> {
    // Ensure signal isn't blocked
    let mut sigset = signal::SigSet::empty();
    sigset.add(sig);
    signal::sigprocmask(signal::SigmaskHow::SIG_UNBLOCK, Some(&sigset), None)?;

    // Install recovery handler
    unsafe {
        signal::sigaction(
            sig,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(recover_from_hardware_error),
                signal::SaFlags::SA_NODEFER,
                signal::SigSet::empty(),
            ),
        )?
    };

    unsafe { LAST_ERROR_SIGNAL = 0 };
    setcontext_and_raise_err();
    assert_eq!(unsafe { LAST_ERROR_SIGNAL }, sig as i32);

    // Restore default action
    unsafe {
        signal::sigaction(
            sig,
            &signal::SigAction::new(
                signal::SigHandler::SigDfl,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    Ok(())
}

fn test_hardware_error_signals() -> Result<(), Box<dyn Error>> {
    //let signals = [Signal::SIGSEGV, Signal::SIGILL, Signal::SIGBUS, Signal::SIGFPE];

    // Allocate the recovery point.
    unsafe { RECOVERY_POINT = Box::leak(Box::<libc::ucontext_t>::new(std::mem::zeroed())) };

    test_hardware_error_signal(signal::SIGSEGV, || {
        // Set our recovery point. The handler will *jump* back to this.
        unsafe { libc::getcontext(RECOVERY_POINT) };
        if unsafe { LAST_ERROR_SIGNAL } != 0 {
            // Already created and recovered from error.
            return;
        }
        unsafe {
            // Dereference NULL pointer
            asm!("mov rax, [0]", out("rax") _);
        }
    })?;

    test_hardware_error_signal(signal::SIGILL, || {
        unsafe { libc::getcontext(RECOVERY_POINT) };
        if unsafe { LAST_ERROR_SIGNAL } != 0 {
            return;
        }
        unsafe {
            // Execute illegal instruction.
            // ud2 is guaranteed to be undefined.
            asm!("ud2");
        }
    })?;

    test_hardware_error_signal(signal::SIGFPE, || {
        unsafe { libc::getcontext(RECOVERY_POINT) };
        if unsafe { LAST_ERROR_SIGNAL } != 0 {
            return;
        }
        unsafe {
            // Divide by zero.
            // Unsigned divide RDX:RAX by r/m64, with result stored in RAX ← Quotient, RDX ← Remainder.
            asm!("div rcx", in("rcx") 0u64, inout("rdx") 0u64 => _, inout("rax") 0u64 => _);
        }
    })?;

    // TODO: SIGBUS

    Ok(())
}

#[derive(Debug, Eq, PartialEq)]
struct DoSetcontextHandlerResult {
    // The value of rax seen by the signal handler.
    ctx_rax: i64,
}
static mut DO_SETCONTEXT_RES: Option<DoSetcontextHandlerResult> = None;

extern "C" fn do_setcontext(
    _signal: i32,
    info: *mut libc::siginfo_t,
    voidctx: *mut std::ffi::c_void,
) {
    assert!(!info.is_null());
    assert!(!voidctx.is_null());
    let ctx = unsafe { &mut *voidctx.cast::<libc::ucontext_t>() };

    // Verify that the signal was received while blocked in a syscall.
    let rip = ctx.uc_mcontext.gregs[libc::REG_RIP as usize] as *const u8;
    assert!(!rip.is_null());
    const SYSCALL_INSN: [u8; 2] = [0x0f, 0x05];
    // The instruction pointer that we *expect* to be pointing to a syscall insn.
    // If it was some other instruction we could end up with a pointer to the
    // middle of an instruction, but that's ok.
    let interrupted_ip = unsafe { rip.sub(SYSCALL_INSN.len()) };
    let interrupted_insn =
        unsafe { std::slice::from_raw_parts(interrupted_ip, SYSCALL_INSN.len()) };
    assert_eq!(interrupted_insn, &SYSCALL_INSN);

    // We don't know here the expected value of RAX; propagate it back to the test function.
    unsafe {
        DO_SETCONTEXT_RES = Some(DoSetcontextHandlerResult {
            ctx_rax: ctx.uc_mcontext.gregs[libc::REG_RAX as usize],
        })
    };

    // *jump* back via setcontext.
    unsafe { libc::setcontext(ctx) };
    unreachable!()
}

// golang and perhaps other language runtimes can inspect and use the `ucontext`
// to transfer control between user-space threads.
//
// The golang runtime definitely inspects the context provided to the signal
// handler to help determine whether it was executing at a point that's safe to
// preempt.  I'm unclear whether it ever uses `setcontext` or `swapcontext`
// directly with that context object, but testing that doing so works seems like
// a fairly strong test that the provided context is "valid".
fn test_validate_context() -> Result<(), Box<dyn Error>> {
    // This is the signal that itimer generates.
    let sig = signal::SIGALRM;

    // Ensure signal isn't blocked
    let mut sigset = signal::SigSet::empty();
    sigset.add(sig);
    signal::sigprocmask(signal::SigmaskHow::SIG_UNBLOCK, Some(&sigset), None)?;

    // Install handler
    unsafe {
        signal::sigaction(
            sig,
            &signal::SigAction::new(
                signal::SigHandler::SigAction(do_setcontext),
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )?
    };

    // Schedule a one-shot signal via itimer
    let timer = libc::itimerval {
        it_interval: libc::timeval {
            tv_sec: 0,
            tv_usec: 0,
        },
        it_value: libc::timeval {
            tv_sec: 0,
            tv_usec: 100_000,
        },
    };
    setitimer(libc::ITIMER_REAL, &timer).unwrap();

    // sleep, which should get interrupted.
    let rv = unsafe { libc::usleep(1_000_000) };
    if !running_in_shadow() {
        // setcontext sets RAX to 0 rather than restoring the original value.
        // This gets interpreted as the interrupted syscall return value, ultimately
        // resulting in a return value of 0 from usleep.
        assert_eq!(rv, 0);
        // The signal handler sees the syscall return value stored in RAX.
        assert_eq!(
            unsafe { DO_SETCONTEXT_RES.take() },
            Some(DoSetcontextHandlerResult {
                ctx_rax: -(libc::EINTR as i64)
            })
        );
    } else {
        // In shadow, setcontext swapped to the *synthetic* context in the shim's
        // signal handling. From there we return normally, ultimately returning
        // the "correct" syscall result that the syscall was interrupted.
        //
        // This isn't ideal since it's a deviation from the native behavior, but
        // go ahead and test for the current-expected behavior.
        //
        // The only practical way I can think of to remove this deviation is to
        // force all syscalls through the seccomp path. This would mean not
        // using the libc-preload "fast path", and changing the patched vdso
        // functions to execute syscalls instead of calling the shim's syscall
        // functions. The former can already be opted into with
        // `--use-preload-libc=false`. This fix is fairly easy, but gives up
        // some performance.
        assert_eq!(rv, -1);
        assert_eq!(unsafe { *libc::__errno_location() }, libc::EINTR);
        // Similarly, the synthetic context won't have the RAX value corresponding
        // to the return value of the interrupted syscall.
        //
        // We probably *could* patch this particular register into the synthetic
        // context, but it's unclear that just "fixing" that register without
        // fixing the others would have much point. Don't assert anything about it;
        // just clear it.
        unsafe { DO_SETCONTEXT_RES.take() };
    }

    // Same thing again, but sleep using a direct syscall, which will be
    // intercepted via seccomp. shadow has the syscall callsite context to provide
    // to the signal handler in this case, so we should end up jumping all the way
    // back to the original syscall site, with setcontext setting the return value to 0,
    // which will get interpreted as the syscall succeeding.
    setitimer(libc::ITIMER_REAL, &timer).unwrap();
    let res = rustix::thread::nanosleep(&Timespec {
        tv_sec: 1,
        tv_nsec: 0,
    });
    // setcontext sets RAX to 0, which gets interpreted as the clock_nanosleep
    // syscall succeeding, which propagates back to success here.
    assert!(matches!(res, NanosleepRelativeResult::Ok));
    // The signal handler sees the original syscall return value stored in RAX.
    assert_eq!(
        unsafe { DO_SETCONTEXT_RES.take() },
        Some(DoSetcontextHandlerResult {
            ctx_rax: -(libc::EINTR as i64)
        })
    );

    // Restore default action
    unsafe {
        signal::sigaction(
            sig,
            &signal::SigAction::new(
                signal::SigHandler::SigDfl,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    // should we restrict the tests we run?
    let filter_shadow_passing = std::env::args().any(|x| x == "--shadow-passing");
    let filter_libc_passing = std::env::args().any(|x| x == "--libc-passing");
    // should we summarize the results rather than exit on a failed test
    let summarize = std::env::args().any(|x| x == "--summarize");

    let all_envs = set![TestEnv::Libc, TestEnv::Shadow];
    let mut tests: Vec<test_utils::ShadowTest<(), Box<dyn Error>>> = vec![
        ShadowTest::new(
            "raise",
            || {
                test_raise(
                    &|s| signal::raise(s).unwrap(),
                    // The documentation for `raise` is ambiguous whether the signal is
                    // thread-directed or process-directed in a single-threaded program,
                    // and different versions of libc have different behavior. Therefore
                    // we don't check the SignalCode.
                    None,
                )
            },
            all_envs.clone(),
        ),
        ShadowTest::new(
            "raise via kill",
            || {
                test_raise(
                    &|s| signal::kill(unistd::getpid(), s).unwrap(),
                    Some(SignalCode::SI_USER),
                )
            },
            all_envs.clone(),
        ),
        ShadowTest::new(
            "raise via tkill",
            || {
                test_raise(
                    &|s| tkill(unistd::gettid(), s).unwrap(),
                    Some(SignalCode::SI_TKILL),
                )
            },
            all_envs.clone(),
        ),
        ShadowTest::new(
            "raise via tgkill",
            || {
                test_raise(
                    &|s| tgkill(unistd::getpid(), unistd::gettid(), s).unwrap(),
                    Some(SignalCode::SI_TKILL),
                )
            },
            all_envs.clone(),
        ),
        ShadowTest::new("sigprocmask", test_sigprocmask, all_envs.clone()),
        ShadowTest::new(
            "send to thread and process",
            test_send_to_thread_and_process,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "raise process targeted signal multithreaded",
            test_process_pending_multithreaded,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "handled tkill interrupts syscall",
            test_handled_tkill_interrupts_syscall,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "ignored tkill doesn't interrupt syscall",
            test_ignored_tkill_doesnt_interrupt_syscall,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "default-ignored signal doesn't interrupt syscall",
            test_default_ignored_tkill_doesnt_interrupt_syscall,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "handled kill interrupts syscall",
            test_handled_kill_interrupts_syscall,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "sigaltstack unconfigured",
            test_sigaltstack_unconfigured,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "sigaltstack unused",
            test_sigaltstack_configured_but_unused,
            all_envs.clone(),
        ),
        ShadowTest::new("sigaltstack used", test_sigaltstack_used, all_envs.clone()),
        ShadowTest::new(
            "sigaltstack autodisarm",
            test_sigaltstack_autodisarm,
            all_envs.clone(),
        ),
        ShadowTest::new("sa_restart", test_restart, all_envs.clone()),
        ShadowTest::new("sa_restart all", test_restart_all, all_envs.clone()),
        // Can't test precise behavior in Linux, since we can't reliably cause multiple
        // signals to be delivered atomically to another thread while it's
        // blocked in another syscall.
        ShadowTest::new(
            "sa_restart first",
            test_restart_first,
            set![TestEnv::Shadow],
        ),
        ShadowTest::new(
            "sa_restart second",
            test_restart_second,
            set![TestEnv::Shadow],
        ),
        ShadowTest::new(
            "synchronous sigsegv",
            test_synchronous_sigsegv,
            all_envs.clone(),
        ),
        ShadowTest::new(
            "hardware error signals",
            test_hardware_error_signals,
            all_envs.clone(),
        ),
        ShadowTest::new("validate context", test_validate_context, all_envs),
    ];

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
