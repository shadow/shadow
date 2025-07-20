/// Regression test for [#1623](https://github.com/shadow/shadow/issues/1623)
///
/// Only designed to run under Shadow, as it relies on Shadow's deterministic scheduling.
fn main() {
    let (read_fd, write_fd) = nix::unistd::pipe2(nix::fcntl::OFlag::O_NONBLOCK).unwrap();

    // Write to the pipe until its buffer is full.
    loop {
        let buffer = [0; 1024];
        match nix::unistd::write(write_fd, &buffer) {
            Ok(_) => (),
            Err(nix::errno::Errno::EWOULDBLOCK) => break,
            Err(e) => panic!("Unexpected result: {e}"),
        }
    }

    // Clear O_NONBLOCK, making subsequent write operations blocking.
    nix::fcntl::fcntl(
        write_fd,
        nix::fcntl::FcntlArg::F_SETFL(nix::fcntl::OFlag::empty()),
    )
    .unwrap();

    let child_finished = std::sync::Arc::new(std::sync::atomic::AtomicBool::new(false));
    {
        let child_finished = child_finished.clone();
        std::thread::spawn(move || {
            // This should initially block.
            assert_eq!(1, nix::unistd::write(write_fd, &[0]).unwrap());
            // Signal to the parent that the `write` returned.
            child_finished.store(true, std::sync::atomic::Ordering::SeqCst);
        })
    };

    // Let the child thread get blocked.
    nix::unistd::sleep(1);

    // Read a single byte, making the write-end writable.
    assert_eq!(nix::unistd::read(read_fd, &mut [0]).unwrap(), 1);

    // In Shadow's current scheduler, the child should not have had a chance to run yet. This could
    // change in the future, but if so this test will no longer be valid as-is.
    assert!(!child_finished.load(std::sync::atomic::Ordering::SeqCst));

    // Exit the whole process. Since we haven't called any blocking syscalls since unblocking the
    // child thread, it shouldn't have had a chance to run yet.
    unsafe { libc::syscall(libc::SYS_exit_group, 0) };

    // Shadow shouldn't crash, despite the the child thread having been scheduled but not yet run.
}
