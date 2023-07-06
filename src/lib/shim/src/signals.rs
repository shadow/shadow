use linux_api::signal::{
    defaultaction, sigaction, siginfo_t, sigset_t, stack_t, SigActionFlags, SigAltStackFlags,
    Signal, SignalHandler,
};
use log::{debug, trace};
use shadow_shim_helper_rs::shim_shmem;

/// Calls the given signal handler. We sometimes call this function via
/// `libc::swapcontext`, so it needs to be `extern "C"`.
///
/// # Safety
///
/// `action` and `siginfo` must be safely dereferenceable. `ucontext` must be
/// safely dereferenceable or NULL.
unsafe extern "C" fn call_signal_handler(
    action: *const sigaction,
    signal: Signal,
    siginfo: *mut siginfo_t,
    ucontext: *mut libc::ucontext_t,
) {
    assert!(crate::global_allow_native_syscalls::swap(false));
    let action = unsafe { action.as_ref().unwrap() };
    match unsafe { action.handler() } {
        linux_api::signal::SignalHandler::Handler(handler_fn) => unsafe {
            handler_fn(signal.into())
        },
        linux_api::signal::SignalHandler::Action(action_fn) => unsafe {
            action_fn(signal.into(), siginfo, ucontext as *mut core::ffi::c_void)
        },
        linux_api::signal::SignalHandler::SigIgn | linux_api::signal::SignalHandler::SigDfl => {
            panic!("No handler")
        }
    }
    assert!(!crate::global_allow_native_syscalls::swap(true));
}

fn die_with_fatal_signal(sig: Signal) -> ! {
    assert!(crate::global_allow_native_syscalls::get());
    if sig == Signal::SIGKILL {
        // No need to restore default action, and trying to do so would fail.
    } else {
        let action = sigaction::new_with_default_restorer(
            SignalHandler::SigDfl,
            SigActionFlags::empty(),
            sigset_t::EMPTY,
        );
        unsafe { linux_api::signal::rt_sigaction(sig, &action, None) }.unwrap();
    }
    let pid = rustix::process::getpid();
    rustix::process::kill_process(pid, rustix::process::Signal::from_raw(sig.into()).unwrap())
        .unwrap();
    unreachable!()
}

/// Handle pending unblocked signals, and return whether *all* corresponding
/// signal actions had the SA_RESTART flag set.
pub fn process_signals(mut ucontext: Option<&mut libc::ucontext_t>) -> bool {
    let mut host = crate::global_host_shmem::get();
    let mut process = crate::global_process_shmem::get();
    let mut thread = crate::tls_thread_shmem::get();
    let mut host_lock = host.protected().lock();

    let mut restartable = true;

    while let Some((sig, mut siginfo)) =
        shim_shmem::take_pending_unblocked_signal(&host_lock, &process, &thread)
    {
        let blocked_signals = thread.protected.borrow(&host_lock.root).blocked_signals;
        let action = *unsafe { process.protected.borrow(&host_lock.root).signal_action(sig) };

        if matches!(unsafe { action.handler() }, SignalHandler::SigIgn) {
            continue;
        }

        if matches!(unsafe { action.handler() }, SignalHandler::SigDfl) {
            match defaultaction(sig) {
                linux_api::signal::LinuxDefaultAction::IGN => continue,
                linux_api::signal::LinuxDefaultAction::CORE
                | linux_api::signal::LinuxDefaultAction::TERM => {
                    drop(host_lock);
                    die_with_fatal_signal(sig);
                }
                linux_api::signal::LinuxDefaultAction::STOP => unimplemented!(),
                linux_api::signal::LinuxDefaultAction::CONT => unimplemented!(),
            }
        }

        trace!("Handling emulated signal {sig:?}");

        let handler_mask = {
            let mut m = action.mask() | blocked_signals;
            if !action.flags_retain().contains(SigActionFlags::SA_NODEFER) {
                m.add(sig)
            }
            m
        };
        thread.protected.borrow_mut(&host_lock.root).blocked_signals = handler_mask;

        if action.flags_retain().contains(SigActionFlags::SA_RESETHAND) {
            // SAFETY: The handler (`SigDfl`) is sound.
            unsafe {
                *process
                    .protected
                    .borrow_mut(&host_lock.root)
                    .signal_action_mut(sig) = sigaction::new_with_default_restorer(
                    SignalHandler::SigDfl,
                    SigActionFlags::empty(),
                    sigset_t::EMPTY,
                )
            };
        }

        if !action.flags_retain().contains(SigActionFlags::SA_RESTART) {
            restartable = false;
        }

        // SAFETY: Pointers in the sigaltstack should are valid in the managed process.
        let ss_original = *unsafe { thread.protected.borrow(&host_lock.root).sigaltstack() };
        if action.flags_retain().contains(SigActionFlags::SA_ONSTACK)
            && !ss_original
                .flags_retain()
                .contains(SigAltStackFlags::SS_DISABLE)
        {
            // Call the handler on the configured stack.

            if ss_original
                .flags_retain()
                .contains(SigAltStackFlags::SS_ONSTACK)
            {
                // Documentation is unclear what should happen, but switching to
                // the already-in-use stack would almost certainly go badly.
                panic!("Alternate stack already in use.")
            }

            // Update the signal-stack configuration while the handler is being run.
            let ss_during_handler = if ss_original
                .flags_retain()
                .contains(SigAltStackFlags::SS_AUTODISARM)
            {
                stack_t::new(core::ptr::null_mut(), SigAltStackFlags::SS_DISABLE, 0)
            } else {
                stack_t::new(
                    ss_original.sp(),
                    ss_original.flags_retain() | SigAltStackFlags::SS_ONSTACK,
                    ss_original.size(),
                )
            };
            // SAFETY: stack pointer in the assigned stack (if any) is valid in
            // the managed process.
            unsafe {
                *thread
                    .protected
                    .borrow_mut(&host_lock.root)
                    .sigaltstack_mut() = ss_during_handler
            };

            // Set up a context that uses the configured signal stack.
            let mut orig_ctx: libc::ucontext_t = unsafe { core::mem::zeroed() };
            let mut handler_ctx: libc::ucontext_t = unsafe { core::mem::zeroed() };
            unsafe { libc::getcontext(&mut handler_ctx) };
            handler_ctx.uc_link = &mut orig_ctx;
            handler_ctx.uc_stack.ss_sp = ss_original.ss_sp;
            handler_ctx.uc_stack.ss_size = ss_original.ss_size.try_into().unwrap();
            // If a context was provided by the caller, we pass that through
            // to the signal handler; it's the caller's responsibility to swap
            // back to that context.
            //
            // Otherwise we pass the pre-stack-switch context we're creating
            // here.  It'll be swapped-back-to when `swapcontext` returns.
            let mut ctx = match &mut ucontext {
                Some(c) => c,
                None => &mut orig_ctx,
            };
            // We have to transmute this function to the signature expected by
            // `makecontext`.
            let func = unsafe {
                core::mem::transmute::<
                    unsafe extern "C" fn(
                        *const sigaction,
                        Signal,
                        *mut siginfo_t,
                        *mut libc::ucontext_t,
                    ),
                    extern "C" fn(),
                >(call_signal_handler)
            };
            unsafe {
                libc::makecontext(
                    &mut handler_ctx,
                    func,
                    4,
                    &action as *const _,
                    sig,
                    &mut siginfo as *mut _,
                    &mut ctx as *mut _,
                )
            };

            // Call the handler on the configured signal stack.
            drop(host_lock);
            drop(host);
            drop(process);
            drop(thread);

            if unsafe { libc::swapcontext(&mut orig_ctx, &handler_ctx) } != 0 {
                panic!("libc::swapcontext");
            }

            host = crate::global_host_shmem::get();
            process = crate::global_process_shmem::get();
            thread = crate::tls_thread_shmem::get();
            host_lock = host.protected().lock();

            // SAFETY: Pointers are valid in managed process.
            unsafe {
                *thread
                    .protected
                    .borrow_mut(&host_lock.root)
                    .sigaltstack_mut() = ss_original
            };
        } else {
            let ctx: *mut libc::ucontext_t = ucontext
                .as_mut()
                .map(|c| *c as *mut _)
                .unwrap_or(core::ptr::null_mut());
            if ctx.is_null() {
                // To handle this case we might be able to use `makecontext`
                // and `swapcontext` as in the sigaltstack case, but we'd need
                // a stack to use for the new context. We could try to partition
                // the current stack, but that's a bit tricky.
                //
                // So far we don't know of any real-world cases that get here
                // and actually dereference the context in the handler.
                debug!("Passing NULL ucontext_t to handler for signal {sig:?}");
            }

            // Call the handler on the configured signal stack.
            drop(host_lock);
            drop(host);
            drop(process);
            drop(thread);

            unsafe { call_signal_handler(&action, sig, &mut siginfo, ctx) };

            host = crate::global_host_shmem::get();
            process = crate::global_process_shmem::get();
            thread = crate::tls_thread_shmem::get();
            host_lock = host.protected().lock();
        }

        // Restore mask
        thread.protected.borrow_mut(&host_lock.root).blocked_signals = blocked_signals;
    }
    restartable
}

mod export {
    use super::*;

    /// Handle pending unblocked signals, and return whether *all* corresponding
    /// signal actions had the SA_RESTART flag set.
    ///
    /// `ucontext` will be passed through to handlers if non-NULL. This should
    /// generally only be done if the caller has a `ucontext` that will be swapped to
    /// after this code returns; e.g. one that was passed to our own signal handler,
    /// which will be swapped to when that handler returns.
    ///
    /// If `ucontext` is NULL, one will be created at the point where we invoke
    /// the handler, and swapped back to when it returns.
    /// TODO: Creating `ucontext_t` is currently only implemented for handlers that
    /// execute on a sigaltstack.
    ///
    /// # Safety
    ///
    /// FIXME
    #[no_mangle]
    pub unsafe extern "C" fn shim_process_signals(ucontext: *mut libc::ucontext_t) -> bool {
        let ucontext = unsafe { ucontext.as_mut() };
        process_signals(ucontext)
    }
}
