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
    let child_thread = {
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

    // At this point the child thread should be scheduled to run again in
    // Shadow, but won't get a chance to do so until this thread blocks. Write a
    // single byte again, making the pipe no longer writable before the child
    // gets a chance to run.
    assert_eq!(nix::unistd::write(write_fd, &[0]).unwrap(), 1);

    // Give the child thread a chance to run. The Shadow callback should run,
    // but detect that the fd is no longer writable, and then *not* run the
    // child.
    nix::unistd::sleep(1);

    // Child should not have finished yet.
    assert!(!child_finished.load(std::sync::atomic::Ordering::SeqCst));

    // Read a single byte, making the write-end writable again.
    assert_eq!(nix::unistd::read(read_fd, &mut [0]).unwrap(), 1);

    // Give the child a chance to run and finish. In the original bug, the
    // child's listener on the file descriptor would be lost, so the child would
    // never get unblocked again.
    nix::unistd::sleep(1);

    // Child should have finished.  We use this assertion to ensure the test
    // explicitly fails even if the joining the child thread below hangs
    // indefinitely.
    assert!(child_finished.load(std::sync::atomic::Ordering::SeqCst));

    // Now let the child finish.
    child_thread.join().unwrap();
}
