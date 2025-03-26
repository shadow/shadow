use std::sync::Arc;
use std::sync::atomic::AtomicBool;
use std::time::Duration;

// One thread waits for another thread to modify memory, with the waitee not
// performing any syscalls. Regression test for
// <https://github.com/shadow/shadow/issues/2066>.
fn test_wait_for_other_thread_with_cpu_only_loop() {
    let ready_flag = Arc::new(<AtomicBool>::new(false));
    let waiter = {
        let ready_flag = ready_flag.clone();
        std::thread::spawn(move || {
            while !ready_flag.load(std::sync::atomic::Ordering::Relaxed) {
                // *nothing* here, particularly nothing that would return
                // control to shadow such as a syscall.
            }
        })
    };
    let waitee = std::thread::spawn(move || {
        // Wait long enough to ensure the spawned thread gets a chance to run
        // and get stuck in its pure-CPU busy loop.
        std::thread::sleep(Duration::from_millis(1));
        // Trigger exit condition of the other thread's busy loop.
        ready_flag.store(true, std::sync::atomic::Ordering::Relaxed);
    });
    waiter.join().unwrap();
    waitee.join().unwrap();
}

fn main() {
    println!("test_wait_for_other_thread_with_cpu_only_loop");
    test_wait_for_other_thread_with_cpu_only_loop();
}
