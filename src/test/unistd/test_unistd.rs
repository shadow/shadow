/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::ffi::CStr;
use std::ffi::CString;
use std::process;
use std::sync::atomic::{AtomicUsize, Ordering};

use test_utils::get_errno;

static SIGACTION_COUNT: AtomicUsize = AtomicUsize::new(0);

extern "C" fn handler(_: libc::c_int) {
    SIGACTION_COUNT.fetch_add(1, Ordering::SeqCst);
}

struct ExpectedName {
    sysname: CString,
    nodename: CString,
    release: CString,
    version: CString,
    machine: CString,
}

fn main() {
    let argv: Vec<CString> = std::env::args()
        .map(|x| CString::new(x.as_bytes()).unwrap())
        .collect();
    println!("{:?}", argv);

    if argv.len() < 6 {
        eprintln!(
            "Usage: {:?} sysname nodename release version machine",
            argv[0].to_str()
        );
        std::process::exit(1);
    }

    let expected_name = ExpectedName {
        sysname: argv[1].clone(),
        nodename: argv[2].clone(),
        release: argv[3].clone(),
        version: argv[4].clone(),
        machine: argv[5].clone(),
    };

    test_getpid_nodeps();
    test_getppid();
    test_gethostname(&expected_name.nodename);
    test_uname(&expected_name);
    test_getpid_kill();
}

/// Tests that the results are plausible, but can't really validate that it's our
/// pid without depending on other functionality.
fn test_getpid_nodeps() {
    let pid = unsafe { libc::getpid() };
    assert!(pid > 0);
    assert_eq!(pid, process::id() as libc::pid_t);
}

fn test_getppid() {
    let ppid = unsafe { libc::getppid() };
    assert!(ppid > 0);
    assert_ne!(ppid, unsafe { libc::getpid() });
    if test_utils::running_in_shadow() {
        // Processes started directly from the shadow config file have ppid=1,
        // since shadow effectively acts as the init process.
        assert_eq!(ppid, 1);
    }
}

fn test_gethostname(nodename: &CStr) {
    /*
    Old but still true commentaries from the previous C version of this test

    // Invalid pointer. Documented to return errno=EFAULT in gethostname(2),
    // but segfaults on Ubuntu 18.
    //
    // g_assert_cmpint(gethostname(NULL+1, 100),==,-1);
    // assert_errno_is(EFAULT);

    // Negative len. Documented to return errno=EINVAL in gethostname(2), but
    // segfaults on Ubuntu 18.
    //
    // g_assert_cmpint(gethostname(buf, -1),==,-1);
    // assert_errno_is(EINVAL);
    */

    let errno = gethostname_with_short_buffer();
    assert_eq!(errno, libc::ENAMETOOLONG);

    let hostname = get_gethostname();
    assert_eq!(hostname.as_c_str(), nodename);
}

fn test_uname(expected_name: &ExpectedName) {
    let mut n = unsafe { std::mem::zeroed() };
    let r = unsafe { libc::uname(&mut n) };

    assert_eq!(r, 0);
    assert_eq!(expected_name.sysname, to_cstr(&n.sysname).into());
    assert_eq!(expected_name.nodename, to_cstr(&n.nodename).into());
    assert_eq!(expected_name.machine, to_cstr(&n.machine).into());
    assert_eq!(expected_name.release, to_cstr(&n.release).into());
    assert_eq!(expected_name.version, to_cstr(&n.version).into());
}

/// Validates that the returned pid is ours by using it to send a signal to ourselves.
fn test_getpid_kill() {
    let pid = process::id();

    let x = libc::sigaction {
        sa_sigaction: handler as *mut libc::c_void as libc::sighandler_t,
        sa_flags: 0,
        sa_mask: unsafe { std::mem::zeroed() },
        sa_restorer: None,
    };
    let rv = unsafe {
        libc::sigaction(
            libc::SIGUSR1,
            &x as *const libc::sigaction,
            std::ptr::null_mut(),
        )
    };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::kill(pid as libc::pid_t, libc::SIGUSR1) };
    assert_eq!(rv, 0);
    assert_eq!(SIGACTION_COUNT.load(Ordering::SeqCst), 1);
}

fn gethostname_with_short_buffer() -> libc::c_int {
    let mut buffer = vec![0u8; 1];
    let err = unsafe { libc::gethostname(buffer.as_mut_ptr() as *mut libc::c_char, buffer.len()) };

    assert_eq!(err, -1);
    get_errno()
}

fn get_gethostname() -> CString {
    let mut buffer = vec![0u8; 1000];
    let err = unsafe { libc::gethostname(buffer.as_mut_ptr() as *mut libc::c_char, buffer.len()) };
    assert_eq!(err, 0);

    let hostname_len = buffer
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(buffer.len());
    buffer.resize(hostname_len + 1, 0);

    CStr::from_bytes_with_nul(&buffer).unwrap().into()
}

fn to_cstr(buf: &[libc::c_char]) -> &CStr {
    unsafe { CStr::from_ptr(buf.as_ptr()) }
}
