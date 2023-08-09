use core::ffi::c_void;
use std::error::Error;
use std::num::NonZeroI32;
use std::sync::atomic::{self, AtomicU32};

use linux_api::errno::Errno;
use linux_api::ldt::linux_user_desc;
use linux_api::sched::CloneFlags;
use linux_api::signal::tgkill;
use rustix::mm::{MapFlags, MprotectFlags, ProtFlags};
use rustix::time::Timespec;
use test_utils::TestEnvironment as TestEnv;
use test_utils::{running_in_shadow, set, ShadowTest};
use vasi_sync::lazy_lock::LazyLock;
use vasi_sync::scchannel::SelfContainedChannel;
use vasi_sync::sync::futex_wait;

const CLONE_TEST_STACK_NBYTES: usize = 4 * 4096;

/// Make an "empty" descriptor, which we use to clear thread-local-storage in
/// child threads.
fn make_empty_tls() -> linux_user_desc {
    let mut desc: linux_user_desc = unsafe { core::mem::zeroed() };
    desc.set_seg_not_present(1);
    desc.set_read_exec_only(1);
    desc
}

fn wait_for_thread_exit(tid: NonZeroI32) {
    let pid = rustix::process::getpid();
    while tgkill(pid.as_raw_nonzero(), tid, None) != Err(Errno::ESRCH) {}
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
    assert!(child > 0);
    THREAD_DONE_CHANNEL.receive().unwrap();
    // Wait until thread has exited before deallocating its stack.
    wait_for_thread_exit(child.try_into().unwrap());
    Ok(())
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
    static CHILD_TID: AtomicU32 = AtomicU32::new(u32::MAX);
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
            CHILD_TID.as_ptr(),
        )
    };
    assert!(child > 0);

    // Wait to be notified of child exit via futex wake on `CHILD_TID`.
    loop {
        match futex_wait(&CHILD_TID, u32::MAX) {
            Ok(0) | Err(rustix::io::Errno::AGAIN) | Err(rustix::io::Errno::INTR) => {
                if CHILD_TID.load(atomic::Ordering::Relaxed) == 0 {
                    // child thread has exited
                    break;
                } else {
                    // spurious wakeup. Shouldn't happen under shadow.
                    assert!(!running_in_shadow());
                    // try again
                }
            }
            other => panic!("Unexpected result: {other:?}"),
        };
    }

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
        ShadowTest::new("clear_tid", test_clone_clear_tid, all_envs),
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
