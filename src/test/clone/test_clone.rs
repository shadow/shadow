use core::ffi::c_void;
use std::error::Error;
use std::fmt::Write;
use std::sync::atomic::{self, AtomicU32};

use formatting_nostd::FormatBuffer;
use linux_api::errno::Errno;
use linux_api::ldt::linux_user_desc;
use linux_api::posix_types::Pid;
use linux_api::sched::{CloneFlags, clone_args};
use linux_api::signal::tgkill;
use rustix::fd::{AsRawFd, BorrowedFd, FromRawFd, IntoRawFd, OwnedFd};
use rustix::fs::{OFlags, SeekFrom};
use rustix::mm::{MapFlags, MprotectFlags, ProtFlags};
use rustix::time::Timespec;
use test_utils::{ShadowTest, set};
use test_utils::{TestEnvironment as TestEnv, result_assert_eq};
use vasi_sync::lazy_lock::LazyLock;
use vasi_sync::scchannel::SelfContainedChannel;
use vasi_sync::sync::{AtomicI32, futex_wait};

const CLONE_TEST_STACK_NBYTES: usize = 4 * 4096;

/// Make an "empty" descriptor, which we use to clear thread-local-storage in
/// child threads.
fn make_empty_tls() -> linux_user_desc {
    let mut desc: linux_user_desc = unsafe { core::mem::zeroed() };
    desc.set_seg_not_present(1);
    desc.set_read_exec_only(1);
    desc
}

fn wait_for_thread_exit(tid: Pid) {
    let pid = rustix::process::getpid();
    while tgkill(pid.into(), tid, None) != Err(Errno::ESRCH) {}
}

struct ThreadStack {
    base: *mut c_void,
    size: usize,
}

impl ThreadStack {
    pub fn new(size: usize) -> Self {
        let base = unsafe {
            rustix::mm::mmap_anonymous(
                core::ptr::null_mut(),
                CLONE_TEST_STACK_NBYTES,
                ProtFlags::READ | ProtFlags::WRITE,
                MapFlags::PRIVATE | MapFlags::STACK,
            )
        }
        .unwrap();

        // Use first page as a guard page.
        unsafe { rustix::mm::mprotect(base, 4096, MprotectFlags::empty()) }.unwrap();

        Self { base, size }
    }

    pub fn top(&self) -> *mut c_void {
        unsafe { self.base.add(self.size) }
    }
}

impl Drop for ThreadStack {
    fn drop(&mut self) {
        unsafe { rustix::mm::munmap(self.base, self.size) }.unwrap()
    }
}

fn test_clone_minimal() -> Result<(), Box<dyn Error>> {
    static THREAD_DONE_CHANNEL: LazyLock<SelfContainedChannel<()>> =
        LazyLock::const_new(SelfContainedChannel::new);
    extern "C" fn thread_fn(_param: *mut c_void) -> i32 {
        // thread-local storage is not set up; don't call libc functions here.

        THREAD_DONE_CHANNEL.send(());
        0
    }
    let mut tls = make_empty_tls();
    let stack = ThreadStack::new(CLONE_TEST_STACK_NBYTES);
    let flags = CloneFlags::CLONE_VM
        | CloneFlags::CLONE_FS
        | CloneFlags::CLONE_FILES
        | CloneFlags::CLONE_SIGHAND
        | CloneFlags::CLONE_THREAD
        | CloneFlags::CLONE_SYSVSEM
        | CloneFlags::CLONE_SETTLS;
    let child = unsafe {
        libc::clone(
            thread_fn,
            stack.top(),
            flags.bits().try_into().unwrap(),
            core::ptr::null_mut(),
            core::ptr::null_mut::<i32>(),
            &mut tls,
        )
    };
    let child = Pid::from_raw(child).unwrap();
    THREAD_DONE_CHANNEL.receive().unwrap();
    // Wait until thread has exited before deallocating its stack.
    wait_for_thread_exit(child);
    Ok(())
}

fn test_bad_flags() -> Result<(), Box<dyn Error>> {
    let mut tls = make_empty_tls();
    let stack = ThreadStack::new(CLONE_TEST_STACK_NBYTES);
    // Same flags as `test_clone_minimal`, except we've added
    // CLONE_CLEAR_SIGHAND, which is incompatible with CLONE_SIGHAND.
    //
    // In https://github.com/shadow/shadow/issues/3290 this particular invalid
    // flag combination would result in a crash due to failing to safely clean
    // up on the error path.
    let flags = CloneFlags::CLONE_VM
        | CloneFlags::CLONE_FS
        | CloneFlags::CLONE_FILES
        | CloneFlags::CLONE_SIGHAND
        | CloneFlags::CLONE_CLEAR_SIGHAND
        | CloneFlags::CLONE_THREAD
        | CloneFlags::CLONE_SYSVSEM
        | CloneFlags::CLONE_SETTLS;
    result_assert_eq(
        unsafe {
            // We have to use clone3 here to be able to pass CLONE_CLEAR_SIGHAND
            linux_api::sched::clone3_raw(
                &clone_args {
                    stack: stack.top() as u64,
                    stack_size: CLONE_TEST_STACK_NBYTES as u64,
                    tls: std::ptr::from_mut(&mut tls) as u64,
                    ..Default::default()
                }
                .with_flags(flags),
                std::mem::size_of::<clone_args>(),
            )
        },
        Err(linux_api::errno::Errno::EINVAL),
        "Expected EINVAL",
    )?;
    Ok(())
}

/// Wait for `tid` to have value 0. Loops with a `FUTEX_WAIT` operation;
/// intended for use with `CLONE_CHILD_CLEARTID`.
fn wait_for_clear_tid(tid: &AtomicU32) {
    let mut current;
    loop {
        current = tid.load(atomic::Ordering::Relaxed);
        if current == 0 {
            break;
        }
        match futex_wait(tid, current, None) {
            Ok(0) => (),
            Err(rustix::io::Errno::AGAIN) | Err(rustix::io::Errno::INTR) => {
                // try again
            }
            other => panic!("Unexpected result: {other:?}"),
        };
    }
}

fn test_clone_clear_tid() -> Result<(), Box<dyn Error>> {
    extern "C" fn thread_fn(_param: *mut c_void) -> i32 {
        // thread-local storage is not set up; don't call libc functions here.

        // Try to give parent a chance to sleep on the tid futex.
        match rustix::thread::nanosleep(&Timespec {
            tv_sec: 0,
            tv_nsec: 1_000_000,
        }) {
            rustix::thread::NanosleepRelativeResult::Ok => (),
            r @ rustix::thread::NanosleepRelativeResult::Interrupted(_)
            | r @ rustix::thread::NanosleepRelativeResult::Err(_) => {
                panic!("Unexpected result: {r:?}")
            }
        };
        0
    }
    let mut tls = make_empty_tls();
    let stack = ThreadStack::new(CLONE_TEST_STACK_NBYTES);
    let child_tid = AtomicU32::new(u32::MAX);
    let flags = CloneFlags::CLONE_VM
        | CloneFlags::CLONE_FS
        | CloneFlags::CLONE_FILES
        | CloneFlags::CLONE_SIGHAND
        | CloneFlags::CLONE_THREAD
        | CloneFlags::CLONE_SYSVSEM
        | CloneFlags::CLONE_SETTLS
        | CloneFlags::CLONE_CHILD_CLEARTID;
    let child = unsafe {
        libc::clone(
            thread_fn,
            stack.top(),
            flags.bits().try_into().unwrap(),
            core::ptr::null_mut(),
            core::ptr::null_mut::<i32>(),
            &mut tls,
            child_tid.as_ptr(),
        )
    };
    assert!(child > 0);

    // Wait to be notified of child exit via futex wake on `CHILD_TID`.
    wait_for_clear_tid(&child_tid);

    Ok(())
}

/// Creates an anonymous temp file with the specified contents, and current
/// position set to beginning of the file.
///
/// Safe to call from threads without thread-local-storage set up.
fn make_tmpfile(contents: &[u8]) -> OwnedFd {
    // It'd be nicer to use O_TMPFILE, but Docker doesn't support it.

    static COUNTER: AtomicI32 = AtomicI32::new(0);
    let counter_val = COUNTER.fetch_add(1, atomic::Ordering::Relaxed);
    let pid = rustix::process::getpid().as_raw_nonzero();
    let mut name = FormatBuffer::<20>::new();
    write!(&mut name, "./test-clone-tmp-{pid}-{counter_val}").unwrap();
    let tmpfile = rustix::fs::open(
        name.as_str(),
        OFlags::CREATE | OFlags::EXCL | OFlags::RDWR,
        rustix::fs::Mode::RWXU,
    )
    .unwrap();
    rustix::fs::unlink(name.as_str()).unwrap();
    let mut written = 0;
    while written < contents.len() {
        written += rustix::io::write(&tmpfile, &contents[written..]).unwrap();
    }
    rustix::fs::seek(&tmpfile, SeekFrom::Start(0)).unwrap();

    tmpfile
}

fn test_clone_files_description_offset(use_clone_files_flag: bool) -> Result<(), Box<dyn Error>> {
    let tmpfile = make_tmpfile(&[1]);

    extern "C" fn thread_fn(fd: *mut c_void) -> i32 {
        // thread-local storage is not set up; don't call libc functions here.

        let fd = unsafe { BorrowedFd::borrow_raw(fd as i32) };
        let mut buf = [0; 1];
        rustix::io::read(fd, &mut buf).unwrap();
        assert_eq!(buf[0], 1);
        0
    }
    let mut tls = make_empty_tls();
    let stack = ThreadStack::new(CLONE_TEST_STACK_NBYTES);
    let child_tid = AtomicU32::new(u32::MAX);
    let mut flags = CloneFlags::CLONE_VM
        | CloneFlags::CLONE_FS
        | CloneFlags::CLONE_SIGHAND
        | CloneFlags::CLONE_THREAD
        | CloneFlags::CLONE_SYSVSEM
        | CloneFlags::CLONE_SETTLS
        | CloneFlags::CLONE_CHILD_CLEARTID;
    if use_clone_files_flag {
        flags |= CloneFlags::CLONE_FILES;
    }
    let child = unsafe {
        libc::clone(
            thread_fn,
            stack.top(),
            flags.bits().try_into().unwrap(),
            tmpfile.as_raw_fd() as *mut core::ffi::c_void,
            core::ptr::null_mut::<i32>(),
            &mut tls,
            child_tid.as_ptr(),
        )
    };
    assert!(child > 0);

    // Wait to be notified of child exit via futex wake on `CHILD_TID`.
    wait_for_clear_tid(&child_tid);

    // whether CLONE_FILES is specified or not, file offsets for file descriptions
    // are shared. i.e. we should always be at position 1 now.
    assert_eq!(rustix::fs::seek(&tmpfile, SeekFrom::Current(0)), Ok(1));

    Ok(())
}

fn test_clone_files_dup(use_clone_files_flag: bool) -> Result<(), Box<dyn Error>> {
    const PARENT_VAL: u8 = 1;
    const CHILD_VAL: u8 = 2;

    let tmpfile = make_tmpfile(&[PARENT_VAL]);

    extern "C" fn thread_fn(fd: *mut c_void) -> i32 {
        // thread-local storage is not set up; don't call libc functions here.

        let new_tmpfile = make_tmpfile(&[CHILD_VAL]);
        let mut orig_tmpfile = unsafe { OwnedFd::from_raw_fd(fd as i32) };
        rustix::io::dup2(&new_tmpfile, &mut orig_tmpfile).unwrap();

        // We had to wrap `orig_tmpfile` in an `OwnedFd` for use with
        // rustix's dup2 API, but the parent thread actually owns it.
        // Leak it so that we don't incorrectly close it when this thread exits.
        let _ = orig_tmpfile.into_raw_fd();

        0
    }
    let mut tls = make_empty_tls();
    let stack = ThreadStack::new(CLONE_TEST_STACK_NBYTES);
    let child_tid = AtomicU32::new(u32::MAX);
    let mut flags = CloneFlags::CLONE_VM
        | CloneFlags::CLONE_FS
        | CloneFlags::CLONE_SIGHAND
        | CloneFlags::CLONE_THREAD
        | CloneFlags::CLONE_SYSVSEM
        | CloneFlags::CLONE_SETTLS
        | CloneFlags::CLONE_CHILD_CLEARTID;
    if use_clone_files_flag {
        flags |= CloneFlags::CLONE_FILES;
    }
    let child = unsafe {
        libc::clone(
            thread_fn,
            stack.top(),
            flags.bits().try_into().unwrap(),
            tmpfile.as_raw_fd() as *mut core::ffi::c_void,
            core::ptr::null_mut::<i32>(),
            &mut tls,
            child_tid.as_ptr(),
        )
    };
    assert!(child > 0);

    // Wait to be notified of child exit via futex wake on `CHILD_TID`.
    wait_for_clear_tid(&child_tid);

    let mut buf = [0; 1];
    assert_eq!(rustix::io::read(&tmpfile, &mut buf).unwrap(), 1);
    // If CLONE_FILES was set, then we should see the effect of the `dup2` done in
    // the child thread; otherwise we shouldn't.
    let expected_val = if use_clone_files_flag {
        CHILD_VAL
    } else {
        PARENT_VAL
    };
    assert_eq!(buf[0], expected_val);

    Ok(())
}

fn test_parent(use_clone_parent_flag: bool) -> Result<(), Box<dyn Error>> {
    extern "C" fn thread_fn(ppid: *mut c_void) -> i32 {
        // thread-local storage is not set up; don't call libc functions here.

        // SAFETY:
        let ppid_channel =
            unsafe { &*(ppid.cast::<SelfContainedChannel<Option<rustix::process::Pid>>>()) };
        ppid_channel.send(rustix::process::getppid());

        0
    }
    let mut tls = make_empty_tls();
    let stack = ThreadStack::new(CLONE_TEST_STACK_NBYTES);
    let child_tid = AtomicU32::new(u32::MAX);
    let mut flags = CloneFlags::CLONE_VM
        | CloneFlags::CLONE_SIGHAND
        | CloneFlags::CLONE_THREAD
        | CloneFlags::CLONE_SETTLS
        | CloneFlags::CLONE_CHILD_CLEARTID;
    if use_clone_parent_flag {
        flags |= CloneFlags::CLONE_FILES;
    }

    let ppid_channel = SelfContainedChannel::<Option<rustix::process::Pid>>::new();
    let child = unsafe {
        libc::clone(
            thread_fn,
            stack.top(),
            flags.bits().try_into().unwrap(),
            std::ptr::from_ref(&ppid_channel) as *mut core::ffi::c_void,
            core::ptr::null_mut::<i32>(),
            &mut tls,
            child_tid.as_ptr(),
        )
    };
    assert!(child > 0);

    // Wait to be notified of child exit via futex wake on `CHILD_TID`.
    wait_for_clear_tid(&child_tid);

    let res = ppid_channel.receive().unwrap();

    // When creating a thread (CLONE_THREAD), the parent of the child is always
    // the same as the parent of the parent, regardless of whether CLONE_PARENT
    // is set.
    let expected_result = rustix::process::getppid();

    assert_eq!(res, expected_result);

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
        ShadowTest::new("minimal", test_clone_minimal, all_envs.clone()),
        ShadowTest::new("bad_flags", test_bad_flags, all_envs.clone()),
        ShadowTest::new("clear_tid", test_clone_clear_tid, all_envs.clone()),
        ShadowTest::new(
            "clone_files_set_description_offset",
            || test_clone_files_description_offset(true),
            all_envs.clone(),
        ),
        ShadowTest::new(
            "clone_files_unset_description_offset",
            || test_clone_files_description_offset(false),
            all_envs.clone(),
        ),
        ShadowTest::new(
            "clone_files_set_dup2",
            || test_clone_files_dup(true),
            all_envs.clone(),
        ),
        ShadowTest::new(
            "clone_files_unset_dup2",
            || test_clone_files_dup(false),
            all_envs.clone(),
        ),
        ShadowTest::new("clone_parent_set", || test_parent(true), all_envs.clone()),
        ShadowTest::new(
            "clone_parent_unset",
            || test_parent(false),
            all_envs.clone(),
        ),
    ];

    // Explicitly reference these to avoid clippy warning about unnecessary
    // clone at point of last usage above.
    drop(all_envs);

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
