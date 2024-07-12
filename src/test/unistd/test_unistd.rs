/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::ffi::CStr;
use std::ffi::CString;
use std::ffi::OsStr;
use std::os::unix::ffi::OsStrExt;
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
    test_getpgrp();
    test_getsid();
    test_mkdir();
    test_mkdirat();
    test_chdir();
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

fn test_getpgrp() {
    let pgrp = unsafe { libc::getpgrp() };
    assert!(pgrp > 0);
    assert_eq!(unsafe { libc::getpgid(0) }, pgrp);
    assert_eq!(unsafe { libc::getpgid(libc::getpid()) }, pgrp);
    if test_utils::running_in_shadow() {
        // Processes started directly from the shadow config file
        // belong to INIT's process group.
        assert_eq!(pgrp, 1);
    }
}

fn test_getsid() {
    let sid = unsafe { libc::getsid(0) };
    assert!(sid > 0);
    if test_utils::running_in_shadow() {
        // Processes started directly from the shadow config file
        // belong to INIT's session.
        assert_eq!(sid, 1);
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
    let rv =
        unsafe { libc::sigaction(libc::SIGUSR1, std::ptr::from_ref(&x), std::ptr::null_mut()) };
    assert_eq!(rv, 0);

    let rv = unsafe { libc::kill(pid as libc::pid_t, libc::SIGUSR1) };
    assert_eq!(rv, 0);
    assert_eq!(SIGACTION_COUNT.load(Ordering::SeqCst), 1);
}

fn test_mkdir() {
    let dirname = c"./newdir";
    let dirpath = OsStr::from_bytes(dirname.to_bytes());
    let mode = libc::S_IRWXU;
    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::mkdir(dirname.as_ptr(), mode)
    })
    .unwrap();

    // Validate that the directory exists with the expected properties.
    let mut stat: libc::stat = unsafe { std::mem::zeroed() };
    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::stat(dirname.as_ptr(), &mut stat)
    })
    .unwrap();
    // Validate that it has the expected permissions.
    assert_eq!(stat.st_mode, libc::S_IFDIR | mode);

    // Trying to create it again should fail.
    assert_eq!(
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdir(dirname.as_ptr(), 0)
        }),
        Err(linux_api::errno::Errno::EEXIST)
    );

    // Clean up.
    std::fs::remove_dir(dirpath).unwrap();

    // Creating a directory with an invalid path component should fail.
    assert_eq!(
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdir(c"./nonexistent/foo".as_ptr(), 0)
        }),
        Err(linux_api::errno::Errno::ENOENT)
    );
}

fn test_mkdirat() {
    // Create an outer directory first, using AT_FDCWD (equivalent to `mkdir`).
    let outerdirname = c"./outerdir";
    {
        let mode = libc::S_IRWXU;
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdirat(libc::AT_FDCWD, outerdirname.as_ptr(), mode)
        })
        .unwrap();
    }

    let dirfd = linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::open(outerdirname.as_ptr(), libc::O_DIRECTORY)
    })
    .unwrap();
    let basename = c"innerdir";
    let mode = libc::S_IRWXU;
    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::mkdirat(dirfd, basename.as_ptr(), mode)
    })
    .unwrap();

    // Validate that the directory exists with the expected properties.
    let mut stat: libc::stat = unsafe { std::mem::zeroed() };
    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::stat(c"./outerdir/innerdir".as_ptr(), &mut stat)
    })
    .unwrap();
    // Validate that it has the expected permissions.
    assert_eq!(stat.st_mode, libc::S_IFDIR | mode);

    // Trying to create it again should fail.
    assert_eq!(
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdirat(dirfd, basename.as_ptr(), 0)
        }),
        Err(linux_api::errno::Errno::EEXIST)
    );

    // Clean up.
    std::fs::remove_dir("./outerdir/innerdir").unwrap();
    std::fs::remove_dir("./outerdir").unwrap();

    // Creating a directory with an invalid path component should fail.
    assert_eq!(
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdirat(dirfd, c"nonexistent/foo".as_ptr(), 0)
        }),
        Err(linux_api::errno::Errno::ENOENT)
    );

    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe { libc::close(dirfd) }).unwrap();

    // Creating a directory with an invalid file descriptor should fail.
    assert_eq!(
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdirat(-1, basename.as_ptr(), 0)
        }),
        Err(linux_api::errno::Errno::EBADF)
    );

    // Using a non-directory fd should fail.
    let tmpfd = {
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::open(
                c"regfile".as_ptr(),
                libc::O_CREAT | libc::O_RDWR,
                libc::S_IRWXU,
            )
        })
        .unwrap()
    };
    assert_eq!(
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::mkdirat(tmpfd, basename.as_ptr(), 0)
        }),
        Err(linux_api::errno::Errno::ENOTDIR)
    );
    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe { libc::close(tmpfd) }).unwrap();
    std::fs::remove_file("regfile").unwrap();
}

fn test_chdir() {
    let start_cwd = std::env::current_dir().unwrap();

    let mut new_dir = start_cwd.clone();
    new_dir.push("newdir");

    let new_dir_cstr = {
        let mut v = Vec::from(new_dir.as_os_str().as_bytes());
        v.push(0);
        CString::from_vec_with_nul(v).unwrap()
    };

    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::mkdir(new_dir_cstr.as_ptr(), libc::S_IRWXU)
    })
    .unwrap();
    linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
        libc::chdir(new_dir_cstr.as_ptr())
    })
    .unwrap();

    // Check that cwd matches the new directory.
    assert_eq!(&std::env::current_dir().unwrap(), &new_dir);

    // Also check that a file created relative to cwd uses the new directory.
    // This is specifically to look for problems related to
    // <https://github.com/shadow/shadow/issues/2960>
    {
        // Create the file.
        let path = c"./tmpfile";
        let fd = linux_api::errno::Errno::result_from_libc_errno(-1, unsafe {
            libc::open(path.as_ptr(), libc::O_CREAT | libc::O_RDWR, libc::S_IRWXU)
        })
        .unwrap();
        // Close it.
        linux_api::errno::Errno::result_from_libc_errno(-1, unsafe { libc::close(fd) }).unwrap();

        // Validate that it exists at the expected path.
        let mut expected_path = new_dir.clone();
        expected_path.push("tmpfile");
        std::fs::metadata(&expected_path).unwrap();

        // Remove the file.
        std::fs::remove_file(expected_path).unwrap();
    }

    // Clean up.
    std::env::set_current_dir(start_cwd).unwrap();
    std::fs::remove_dir(&new_dir).unwrap();
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
