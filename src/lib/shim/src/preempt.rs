//! Native preemption for managed code.
//!
//! This module is used to regain control from managed code that would otherwise
//! run indefinitely (or for a long time) without otherwise returning control to
//! Shadow. When control is regained in this way, simulated time is moved
//! forward some amount, and the current thread potentially rescheduled.
//!
//! `process_init` should be called once per process, before any other methods are called.
//!
//! `enable` should be called to enable preemption for the current thread, and
//! `disable` to disable preemption for the current thread.
use linux_api::signal::{SigActionFlags, siginfo_t, sigset_t};
use log::{debug, trace};
use shadow_shim_helper_rs::option::FfiOption;
use shadow_shim_helper_rs::shadow_syscalls::ShadowSyscallNum;
use shadow_shim_helper_rs::shim_event::ShimEventSyscall;
use shadow_shim_helper_rs::syscall_types::SyscallArgs;

use crate::ExecutionContext;

// The signal we use for preemption.
const PREEMPTION_SIGNAL: linux_api::signal::Signal = linux_api::signal::Signal::SIGVTALRM;

extern "C" fn handle_timer_signal(signo: i32, _info: *mut siginfo_t, _ctx: *mut core::ffi::c_void) {
    let prev = ExecutionContext::Shadow.enter();
    trace!("Got preemption timer signal.");

    assert_eq!(signo, i32::from(PREEMPTION_SIGNAL));

    if prev.ctx() == ExecutionContext::Shadow {
        // There's a small chance of us getting here when the timer signal fires
        // just as we're switching contexts. It's simpler to just ignore it here than
        // to completely prevent this possibility.
        trace!("Got timer signal in shadow context. Ignoring.");
        return;
    }

    let FfiOption::Some(config) = &crate::global_manager_shmem::get().native_preemption_config
    else {
        // Not configured.
        panic!("Preemption signal handler somehow invoked when it wasn't configured.");
    };

    // Preemption should be rare. Probably worth at least a debug-level message.
    debug!(
        "Native preemption incrementing simulated CPU latency by {:?} after waiting {:?}",
        config.sim_duration, config.native_duration
    );

    {
        // Move simulated time forward.
        let host = crate::global_host_shmem::get();
        let mut host_lock = host.protected().lock();
        host_lock.unapplied_cpu_latency += config.sim_duration;
    }
    // Transfer control to shadow, which will handle the time update and potentially
    // reschedule this thread.
    //
    // We *could* try to apply the cpu-latency here and avoid yielding to shadow
    // if we haven't yet reached the maximum runahead time, as we do in
    // `shim_sys_handle_syscall_locally`, but in practice `config.sim_duration`
    // should be large enough that shadow will always choose to reschedule this
    // thread anyway, so we wouldn't actually get any performance benefit in
    // exchange for the additional complexity.
    let syscall_event = ShimEventSyscall {
        syscall_args: SyscallArgs {
            number: i64::from(u32::from(ShadowSyscallNum::shadow_yield)),
            args: [0.into(); 6],
        },
    };
    unsafe { crate::syscall::emulated_syscall_event(None, &syscall_event) };
}

/// Initialize state for the current native process. This does not yet actually
/// enable preemption, which is done by calling `enable`.
pub fn process_init() {
    debug_assert_eq!(ExecutionContext::current(), ExecutionContext::Shadow);
    let FfiOption::Some(_config) = &crate::global_manager_shmem::get().native_preemption_config
    else {
        // Not configured.
        return;
    };

    let handler = linux_api::signal::SignalHandler::Action(handle_timer_signal);
    let flags = SigActionFlags::SA_SIGINFO | SigActionFlags::SA_ONSTACK;
    let mask = sigset_t::EMPTY;
    let action = linux_api::signal::sigaction::new_with_default_restorer(handler, flags, mask);
    unsafe { linux_api::signal::rt_sigaction(PREEMPTION_SIGNAL, &action, None).unwrap() };
}

/// Disable preemption for the current thread.
pub fn disable() {
    debug_assert_eq!(ExecutionContext::current(), ExecutionContext::Shadow);
    let Some(manager_shmem) = &crate::global_manager_shmem::try_get() else {
        // Not initialized yet. e.g. we get here the first time we enter the
        // Shadow execution context, before completing initialization.
        // In any case, there should be nothing to disable.
        return;
    };
    let FfiOption::Some(_config) = &manager_shmem.native_preemption_config else {
        // Not configured.
        return;
    };

    log::trace!("Disabling preemption");

    // Disable the itimer, effectively discarding any CPU-time we've spent.
    //
    // Functionality-wise this isn't *strictly* required for purposes of
    // supporting cpu-only-busy-loop-escape, since we also block the signal
    // below. However we currently want to minimize the effects of this feature
    // on the simulation, and hence don't want to "accumulate" progress towards
    // the timer firing and then cause regular preemptions even in the absence
    // of long cpu-only operations.
    //
    // Allowing such accumulation is also undesirable since we currently use a
    // process-wide itimer, with the `ITIMER_VIRTUAL` clock that measures
    // process-wide CPU time.  Hence time spent running in one thread without
    // the timer firing would bring *all* threads in that process closer to
    // firing. That issue *could* be addressed by using `timer_create` timers,
    // which support a thread-cpu-time clock `CLOCK_THREAD_CPUTIME_ID`.
    let zero = linux_api::time::kernel_old_timeval {
        tv_sec: 0,
        tv_usec: 0,
    };
    linux_api::time::setitimer(
        linux_api::time::ITimerId::ITIMER_VIRTUAL,
        &linux_api::time::kernel_old_itimerval {
            it_interval: zero,
            it_value: zero,
        },
        None,
    )
    .unwrap();

    // Block the timer's signal for this thread.
    // We're using a process-wide signal, so need to do this to ensure *this*
    // thread doesn't get awoken if shadow ends up suspending this thread and
    // running another, and that thread re-enables the timer and has it fire.
    //
    // We *could* consider using timers created via `timer_create`, which
    // supports being configured to fire thread-targeted signals, and thus
    // wouldn't require us to unblock and re-block the signal when enabling and
    // disabling. However, we'd probably then want to *destroy* the timer when
    // disabling, and re-create when enabling, to avoid bumping into the system
    // limit on the number of such timers (and any potential undocumented
    // scalability issues with having a large number of such timers). This is
    // likely to be at least as expensive as blocking and unblocking the signal.
    linux_api::signal::rt_sigprocmask(
        linux_api::signal::SigProcMaskAction::SIG_BLOCK,
        &PREEMPTION_SIGNAL.into(),
        None,
    )
    .unwrap();
}

/// Enable preemption for the current thread.
///
/// # Safety
///
/// Preemption must not currently be enabled for any other threads in the current process.
pub unsafe fn enable() {
    debug_assert_eq!(ExecutionContext::current(), ExecutionContext::Shadow);
    let FfiOption::Some(config) = &crate::global_manager_shmem::get().native_preemption_config
    else {
        return;
    };
    log::trace!(
        "Enabling preemption with native duration {:?}",
        config.native_duration
    );
    linux_api::time::setitimer(
        linux_api::time::ITimerId::ITIMER_VIRTUAL,
        &linux_api::time::kernel_old_itimerval {
            // We *usually* don't need the timer to repeat, since normally it'll
            // fire when we're in the managed application context, and the
            // signal handler will cause the timer to be re-armed after
            // finishing. However there are some edge cases where the timer can
            // fire while in the shim context, in which case the signal handler
            // just returns, and the timer won't be re-armed. We *could*
            // explicitly re-arm the timer there, but probably more robust to
            // just have an interval here.
            it_interval: config.native_duration,
            it_value: config.native_duration,
        },
        None,
    )
    .unwrap();
    // Allow this thread to receive the preemption signal, which we would have
    // blocked in the last call to `disable`.
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
    pub extern "C-unwind" fn preempt_process_init() {
        process_init();
    }
}
