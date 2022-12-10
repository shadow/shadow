use nix::sys::signal;
use test_utils::setitimer;

pub fn main() {
    // Ignore SIGALRM, so that it doesn't inadvertently kill
    // the process if the timer fires before this process exits.
    unsafe {
        signal::sigaction(
            signal::Signal::SIGALRM,
            &signal::SigAction::new(
                signal::SigHandler::SigIgn,
                signal::SaFlags::empty(),
                signal::SigSet::empty(),
            ),
        )
        .unwrap()
    };

    // Set a timer to run forever.
    setitimer(
        libc::ITIMER_REAL,
        &libc::itimerval {
            it_value: libc::timeval {
                tv_sec: 0,
                tv_usec: 1_000,
            },
            // Repeat every ms indefinitely
            it_interval: libc::timeval {
                tv_sec: 0,
                tv_usec: 1_000,
            },
        },
    )
    .unwrap();

    // exit, with the timer still scheduled.
    // This is to validate the behavior in Shadow when the process no longer
    // exists at the time that the timer event fires.
}
