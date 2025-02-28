use core::cell::Cell;

use linux_api::signal::{
    defaultaction, sigaction, siginfo_t, sigset_t, stack_t, SigActionFlags, SigAltStackFlags,
    Signal, SignalHandler,
};
use log::{debug, trace, warn};
use shadow_shim_helper_rs::shim_shmem;
use shadow_shim_helper_rs::simulation_time::SimulationTime;
use shadow_shim_helper_rs::syscall_types::SyscallArgs;

use crate::bindings::shim_syscall;
use crate::tls::ShimTlsVar;
use crate::{global_host_shmem, tls_process_shmem, tls_thread_shmem, ExecutionContext};

// The signal we use for preemption.
const PREEMPTION_SIGNAL: linux_api::signal::Signal = linux_api::signal::Signal::SIGVTALRM;

// XXX use this (or replace with config)
const PREEMPTION_REAL_CPU_INTERVAL: core::time::Duration = core::time::Duration::from_millis(10);

extern "C" fn handle_timer_signal(signo: i32, _info: *mut siginfo_t, _ctx: *mut core::ffi::c_void) {
    let _prev = ExecutionContext::Shadow.enter();
    // `_prev` should *usually* be `ExecutionContext::Application`, but we don't assert it because
    // it'd be difficult to fully guarantee around the places where we transition between the two.
    // Getting here during such transitions *should* be ok.

    debug!("XXX handle_timer_signal");
    assert_eq!(signo, i32::from(PREEMPTION_SIGNAL));
    debug!("XXX moving time forward");
    {
        // Move time forward enough that shadow will preempt this thread when it
        // gets control again.
        let host = crate::global_host_shmem::get();
        let mut host_lock = host.protected().lock();
        debug!(
            "XXX max_unapplied_cpu_latency: {:?}",
            host.max_unapplied_cpu_latency
        );
        //XXX how much to do here? Using max_unapplied_cpu_latency makes simulated time
        // go much slower than real time with default settings.
        //XXX host_lock.unapplied_cpu_latency = host.max_unapplied_cpu_latency + SimulationTime::from_nanos(1);
        host_lock.unapplied_cpu_latency += SimulationTime::from_millis(10);
    }
    // Transfer control to shadow, which should move time forward and reschedule
    // this thread.
    // XXX dedupe
    let shadow_yield = 1005;
    debug!("XXX pre-yield");
    // XXX: maybe after exe-context, we can do something simpler here.
    // If we directly call `shim_syscall` here, we can't provide a signal
    // context, since `shim_syscall`, specifically wants the context from the
    // SIGSYS/seccomp signal handler. If we don't provide a context, it may
    // still try to handle emulated signals, but without passing a context
    // through to *those* handlers, which isn't ideal.
    //
    // We work around this intentionally triggering a SIGSYS here. To do *that*
    // we go through libc's syscall function, since our seccomp filter allows
    // direct syscalls from the shim without raising SIGSYS. This isn't ideal
    // for trying to completely not depend on libc from the shim, though this
    // particular usage should be safe.
    unsafe { libc::syscall(shadow_yield) };
    debug!("XXX post-yield");
    // If we ended up yielding, the timer is now disabled. reenable.

}

pub unsafe fn process_init() {
    debug_assert_eq!(ExecutionContext::current(), ExecutionContext::Shadow);

    let handler = linux_api::signal::SignalHandler::Action(handle_timer_signal);
    let flags = SigActionFlags::SA_SIGINFO | SigActionFlags::SA_ONSTACK;
    let mask = sigset_t::EMPTY;
    let action = linux_api::signal::sigaction::new_with_default_restorer(handler, flags, mask);
    unsafe { linux_api::signal::rt_sigaction(PREEMPTION_SIGNAL, &action, None).unwrap() };
}

pub unsafe fn disable() {
    debug_assert_eq!(ExecutionContext::current(), ExecutionContext::Shadow);
    let interval = linux_api::time::kernel_old_timeval {
        tv_sec: 0,
        tv_usec: 0,
    };
    linux_api::time::setitimer(
        linux_api::time::ITimerId::ITIMER_VIRTUAL,
        &linux_api::time::kernel_old_itimerval {
            it_interval: interval,
            it_value: interval,
        },
        None,
    )
    .unwrap();
    linux_api::signal::rt_sigprocmask(
        linux_api::signal::SigProcMaskAction::SIG_BLOCK,
        &PREEMPTION_SIGNAL.into(),
        None,
    )
    .unwrap();
}

pub unsafe fn enable() {
    debug_assert_eq!(ExecutionContext::current(), ExecutionContext::Shadow);
    let interval = linux_api::time::kernel_old_timeval {
        tv_sec: 0,
        tv_usec: 10_000,
    };
    linux_api::time::setitimer(
        linux_api::time::ITimerId::ITIMER_VIRTUAL,
        &linux_api::time::kernel_old_itimerval {
            it_interval: interval,
            it_value: interval,
        },
        None,
    )
    .unwrap();
    linux_api::signal::rt_sigprocmask(
        linux_api::signal::SigProcMaskAction::SIG_UNBLOCK,
        &PREEMPTION_SIGNAL.into(),
        None,
    )
    .unwrap();
}

mod export {
    use super::*;

    #[unsafe(no_mangle)]
    pub unsafe extern "C-unwind" fn preempt_process_init() {
        unsafe { process_init() };
    }
}
