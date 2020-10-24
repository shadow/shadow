/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::ffi::CStr;
use std::process;
use std::sync::atomic::{AtomicUsize, Ordering};
use test_utils::running_in_shadow;

static SIGACTION_COUNT: AtomicUsize = AtomicUsize::new(0);

extern "C" {
    pub fn gethostname(name: *mut libc::c_char, size: libc::size_t) -> libc::c_int;
}

extern "C" fn handler(_: libc::c_int) {
    SIGACTION_COUNT.fetch_add(1, Ordering::SeqCst);
}

struct ExpectedName {
    sysname: String,
    nodename: String,
    release: String,
    version: String,
    machine: String,
}

fn main() {
    let argv: Vec<String> = std::env::args().collect();
    println!("{:?}", argv);

    if argv.len() < 6 {
        eprintln!(
            "Usage: {} sysname nodename release version machine",
            argv[0].clone()
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
    test_gethostname(&expected_name.nodename);

    if !running_in_shadow() {
        // TODO: Implement uname in shadow
        test_uname(&expected_name);
        // TODO: Support `kill` in shadow (and/or find another way of validating the pid)
        test_getpid_kill();
    }
}

// Tests that the results are plausible, but can't really validate that it's our
// pid without depending on other functionality.
fn test_getpid_nodeps() {
    let pid = process::id();
    assert!(pid > 0);
    assert_eq!(pid, process::id());
}

fn test_gethostname(nodename: &String) {
    let hostname = get_gethostname();

    assert_eq!(hostname, *nodename);
}

fn test_uname(expected_name: &ExpectedName) {
    unsafe {
        let mut n = std::mem::zeroed();
        let r = libc::uname(&mut n);

        assert_eq!(r, 0);
        assert_eq!(
            expected_name.sysname,
            to_cstr(&n.sysname[..]).to_string_lossy().into_owned()
        );
        assert_eq!(
            expected_name.nodename,
            to_cstr(&n.nodename[..]).to_string_lossy().into_owned()
        );
        assert_eq!(
            expected_name.machine,
            to_cstr(&n.machine[..]).to_string_lossy().into_owned()
        );
        assert_eq!(
            expected_name.release,
            to_cstr(&n.release[..]).to_string_lossy().into_owned()
        );
        assert_eq!(
            expected_name.version,
            to_cstr(&n.version[..]).to_string_lossy().into_owned()
        );
    }
}

// Validates that the returned pid is ours by using it to send a signal to ourselves.
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

    let rv = unsafe { libc::kill(pid as i32, libc::SIGUSR1) };
    assert_eq!(rv, 0);
    assert_eq!(SIGACTION_COUNT.load(Ordering::SeqCst), 1);
}

fn get_gethostname() -> String {
    let mut buffer = vec![0 as u8; 1000];
    let err = unsafe { gethostname(buffer.as_mut_ptr() as *mut libc::c_char, buffer.len()) };
    assert_eq!(err, 0);

    let hostname_len = buffer
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(buffer.len());
    buffer.resize(hostname_len, 0);

    match String::from_utf8(buffer) {
        Ok(hostname) => hostname,
        _ => panic!("Error on String convertion"),
    }
}

fn to_cstr(buf: &[libc::c_char]) -> &CStr {
    unsafe { CStr::from_ptr(buf.as_ptr()) }
}
