use core::fmt::Write;
use core::sync::atomic;

use formatting_nostd::{BorrowedFdWriter, FormatBuffer};
use linux_api::errno::Errno;
use linux_api::ucontext::ucontext;
use rustix::fd::BorrowedFd;
use shadow_shim_helper_rs::emulated_time::EmulatedTime;
use shadow_shim_helper_rs::option::FfiOption;
use shadow_shim_helper_rs::shim_event::{
    ShimEventAddThreadParentRes, ShimEventSyscall, ShimEventSyscallComplete, ShimEventToShadow,
    ShimEventToShim,
};
use shadow_shim_helper_rs::syscall_types::{SysCallArgs, SysCallReg};
use shadow_shim_helper_rs::util::time::TimeParts;

use crate::{bindings, global_host_shmem, tls_ipc, tls_thread_shmem};

/// # Safety
///
/// The specified syscall must be safe to make.
unsafe fn native_syscall(args: &SysCallArgs) -> SysCallReg {
    if args.number == libc::SYS_clone {
        panic!("Shouldn't get here. Should have gone through ShimEventAddThreadReq");
    } else if args.number == libc::SYS_exit {
        let exit_status = args.args[0];
        let exit_status = i32::try_from(exit_status).unwrap_or_else(|_| {
            log::debug!("Truncating thread exit status {exit_status:?}");
            i64::from(exit_status) as i32
        });
        // This thread is exiting. Arrange for its thread-local-storage and
        // signal stack to be freed.
        unsafe { bindings::shim_freeSignalStack() };
        // SAFETY: We don't try to recover from panics.
        // TODO: make shim fully no_std and install a panic handler that aborts.
        // https://doc.rust-lang.org/nomicon/panic-handler.html
        unsafe { crate::release_and_exit_current_thread(exit_status) };
    } else {
        let rv: i64;
        // SAFETY: Caller is responsible for ensuring this syscall is safe to make.
        unsafe {
            core::arch::asm!(
                "syscall",
                inout("rax") args.number => rv,
                in("rdi") u64::from(args.args[0]),
                in("rsi") u64::from(args.args[1]),
                in("rdx") u64::from(args.args[2]),
                in("r10") u64::from(args.args[3]),
                in("r8") u64::from(args.args[4]),
                in("r9") u64::from(args.args[5]))
        };
        return rv.into();
    }
}

/// # Safety
///
/// `ctx` must be valid if provided.
unsafe fn emulated_syscall_event(
    mut ctx: Option<&mut ucontext>,
    syscall_event: &ShimEventSyscall,
) -> SysCallReg {
    log::trace!(
        "sending syscall {} event",
        syscall_event.syscall_args.number
    );

    crate::tls_ipc::with(|ipc| {
        ipc.to_shadow()
            .send(ShimEventToShadow::Syscall(*syscall_event))
    });

    loop {
        log::trace!("waiting for event");
        let res = crate::tls_ipc::with(|ipc| ipc.from_shadow().receive().unwrap());
        log::trace!("got response {res:?}");

        match res {
            ShimEventToShim::SyscallComplete(syscall_complete) => {
                // Shadow has returned a result for the emulated syscall

                if crate::global_host_shmem::try_get().is_none()
                    || crate::global_process_shmem::try_get().is_none()
                {
                    // We should only get here during early initialization. We don't have what
                    // we need to process signals yet, so just return the result.
                    return syscall_complete.retval;
                }

                if let Some(ctx) = ctx.as_mut() {
                    // Set the syscall return value now, before potentially
                    // invoking signal handlers. This appears to be the behavior
                    // in the kernel; i.e. a handler for a signal that
                    // is interrupted a blocking syscall should see the syscall
                    // result (-EINTR) in the context passed to that handler.
                    ctx.uc_mcontext.rax = syscall_complete.retval.into();
                }

                // SAFETY: `ctx` should be valid if present.
                let all_sigactions_had_sa_restart =
                    unsafe { crate::signals::process_signals(ctx.as_deref_mut()) };

                if i64::from(syscall_complete.retval) == Errno::EINTR.to_negated_i64()
                    && all_sigactions_had_sa_restart
                    && syscall_complete.restartable
                {
                    // Restart syscall interrupted syscall
                    crate::tls_ipc::with(|ipc| {
                        ipc.to_shadow()
                            .send(ShimEventToShadow::Syscall(*syscall_event))
                    });
                    continue;
                } else {
                    // Return syscall result
                    return syscall_complete.retval;
                }
            }
            ShimEventToShim::SyscallDoNative => {
                // "Emulate" the syscall by executing it natively.

                let rv = unsafe { native_syscall(&syscall_event.syscall_args) };

                if let FfiOption::Some(strace_fd) = crate::global_process_shmem::get().strace_fd {
                    let emulated_time = global_host_shmem::get()
                        .sim_time
                        .load(atomic::Ordering::Relaxed)
                        - EmulatedTime::SIMULATION_START;
                    let tid = tls_thread_shmem::with(|thread| thread.tid);
                    let parts = TimeParts::from_nanos(emulated_time.as_nanos());
                    let mut buffer = FormatBuffer::<200>::new();
                    writeln!(
                        &mut buffer,
                        "{} [tid {}] ^^^ = {:?}",
                        parts.fmt_hr_min_sec_nano(),
                        tid,
                        rv
                    )
                    .unwrap();
                    // SAFETY: file descriptor should be valid and open.
                    let strace_fd = unsafe { BorrowedFd::borrow_raw(strace_fd) };
                    let mut strace_file_writer = BorrowedFdWriter::new(strace_fd);
                    strace_file_writer.write_str(buffer.as_str()).unwrap();
                }

                return rv;
            }
            ShimEventToShim::Syscall(syscall) => {
                // Execute the syscall and return the result to Shadow.

                let res = unsafe { native_syscall(&syscall.syscall_args) };
                tls_ipc::with(|ipc| {
                    ipc.to_shadow().send(ShimEventToShadow::SyscallComplete(
                        ShimEventSyscallComplete {
                            retval: res,
                            restartable: false,
                        },
                    ))
                });
            }
            ShimEventToShim::AddThreadReq(r) => {
                // Create a new native thread under our control

                let clone_res = unsafe { crate::clone::do_clone(ctx.as_mut().unwrap(), &r) };
                tls_ipc::with(|ipc| {
                    ipc.to_shadow().send(ShimEventToShadow::AddThreadParentRes(
                        ShimEventAddThreadParentRes { clone_res },
                    ))
                })
            }
            e @ ShimEventToShim::StartRes => {
                panic!("Unexpected event: {e:?}");
            }
        }
    }
}

pub mod export {
    use super::*;
    use shadow_shim_helper_rs::shim_event::ShimEventSyscall;
    use shadow_shim_helper_rs::syscall_types::{SysCallArgs, SysCallReg};

    /// # Safety
    ///
    /// `ctx` must be valid if provided.
    #[no_mangle]
    pub unsafe extern "C" fn shim_emulated_syscallv(
        ctx: *mut libc::ucontext_t,
        n: core::ffi::c_long,
        mut args: va_list::VaList,
    ) -> core::ffi::c_long {
        let old_native_syscall_flag = crate::tls_allow_native_syscalls::swap(true);

        let syscall_args = SysCallArgs {
            number: n,
            args: core::array::from_fn(|_| {
                // SAFETY: syscall args all "fit" in an i64. Reading more arguments
                // than actually provided is sound because any bit pattern is a
                // valid i64.
                let arg = unsafe { args.get::<i64>() };
                SysCallReg::from(arg)
            }),
        };

        let event = ShimEventSyscall { syscall_args };

        let ctx = ctx.cast::<ucontext>();
        let ctx = unsafe { ctx.as_mut() };
        let retval = unsafe { emulated_syscall_event(ctx, &event) };

        crate::tls_allow_native_syscalls::swap(old_native_syscall_flag);

        retval.into()
    }

    /// # Safety
    ///
    /// The specified syscall must be safe to make.
    #[no_mangle]
    pub unsafe extern "C" fn shim_native_syscallv(
        n: core::ffi::c_long,
        mut args: va_list::VaList,
    ) -> core::ffi::c_long {
        let syscall_args = SysCallArgs {
            number: n,
            args: core::array::from_fn(|_| {
                // SAFETY: syscall args all "fit" in an i64. Reading more arguments
                // than actually provided is sound because any bit pattern is a
                // valid i64.
                let arg = unsafe { args.get::<i64>() };
                SysCallReg::from(arg)
            }),
        };
        // SAFETY: Ensured by caller.
        let rv = unsafe { native_syscall(&syscall_args) };
        rv.into()
    }
}
