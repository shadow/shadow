/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

use std::error::Error;

// Should be < the stoptime from the test config for this test to be meaningful
const NUM_SECONDS: u64 = 3;

fn main() -> Result<(), Box<dyn Error>> {
    // Test the case where the thread group leader exits via the `exit` syscall (not the `exit`
    // function that actually uses the `exit_group` syscall), leaving a child thread running.

    std::thread::spawn(|| {
        println!("Child started");
        std::thread::sleep(std::time::Duration::from_secs(NUM_SECONDS));
        println!("Child done");
    });

    // Exit only this thread.
    unsafe { libc::syscall(libc::SYS_exit, 0) };

    panic!("Exit didn't exit: {}", nix::errno::Errno::last());
}
