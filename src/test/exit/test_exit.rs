/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

fn main() {
    // Test exiting the process without calling global destructors.
    // e.g. before fixing #1476 this caused Shadow to deadlock.
    unsafe { libc::syscall(libc::SYS_exit_group, 0) };

    // Shouldn't be possible to get here.
    panic!("Failed to exit");
}
