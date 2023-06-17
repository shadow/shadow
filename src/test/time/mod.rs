use std::time::Duration;

/// When we go to sleep, this is the tolerance we allow when checking that we slept the correct
/// amount of time (allows for imprecise kernel wakeups or thread preemption).
pub const SLEEP_TOLERANCE: Duration = Duration::from_millis(30);

/// When we make a sleep syscall that returns immediately, this is the tolerance we allow when
/// checking that we did not sleep (allows for syscall execution time).
pub const SYSCALL_EXEC_TOLERANCE: Duration = Duration::from_micros(500);
