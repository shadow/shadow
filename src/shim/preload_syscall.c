#include <alloca.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include "main/host/syscall/kernel_types.h"
#include "main/shmem/shmem_allocator.h"
#include "shim/ipc.h"
#include "shim/shim.h"
#include "shim/shim_event.h"
#include "shim/shim_logger.h"
#include "shim/shim_shmem.h"
#include "support/logger/logger.h"

static long shadow_retval_to_errno(long retval) {
    // Linux reserves -1 through -4095 for errors. See
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
    if (retval <= -1 && retval >= -4095) {
        errno = -retval;
        return -1;
    }
    return retval;
}

static long _vreal_syscall(long n, va_list args) {
    long arg1 = va_arg(args, long);
    long arg2 = va_arg(args, long);
    long arg3 = va_arg(args, long);
    long arg4 = va_arg(args, long);
    long arg5 = va_arg(args, long);
    long arg6 = va_arg(args, long);
    long rv;

    // r8, r9, and r10 aren't supported as register-constraints in
    // extended asm templates. We have to use [local register
    // variables](https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html)
    // instead. Calling any functions in between the register assignment and the
    // asm template could clobber these registers, which is why we don't do the
    // assignment directly above.
    register long r10 __asm__("r10") = arg4;
    register long r8 __asm__("r8") = arg5;
    register long r9 __asm__("r9") = arg6;
    __asm__ __volatile__("syscall"
                         : "=a"(rv)
                         : "a"(n), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
    return shadow_retval_to_errno(rv);
}

// Handle to the real syscall function, initialized once at load-time for
// thread-safety.
static long _real_syscall(long n, ...) {
    va_list args;
    va_start(args, n);
    long rv = _vreal_syscall(n, args);
    va_end(args);
    return rv;
}

static SysCallReg _shadow_syscall_event(const ShimEvent* syscall_event) {

    ShMemBlock ipc_blk = shim_thisThreadEventIPCBlk();

    debug("sending syscall %ld event on %p", syscall_event->event_data.syscall.syscall_args.number,
          ipc_blk.p);

    shimevent_sendEventToShadow(ipc_blk.p, syscall_event);
    SysCallReg rv = {0};

    // By default we assume Shadow will return quickly, and so should spin
    // rather than letting the OS block this thread.
    bool spin = true;
    while (true) {
        debug("waiting for event on %p", ipc_blk.p);
        ShimEvent res = {0};
        shimevent_recvEventFromShadow(ipc_blk.p, &res, spin);
        debug("got response of type %d on %p", res.event_id, ipc_blk.p);
        // Reset spin-flag to true. (May have been set to false by a SHD_SHIM_EVENT_BLOCK in the
        // previous iteration)
        spin = true;
        switch (res.event_id) {
            case SHD_SHIM_EVENT_BLOCK: {
                // Loop again, this time relinquishing the CPU while waiting for the next message.
                spin = false;
                // Ack the message.
                shimevent_sendEventToShadow(ipc_blk.p, &res);
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                // Use provided result.
                SysCallReg rv = res.event_data.syscall_complete.retval;
                shimlogger_set_simulation_nanos(res.event_data.syscall_complete.simulation_nanos);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL_DO_NATIVE: {
                // Make the original syscall ourselves and use the result.
                SysCallReg rv = res.event_data.syscall_complete.retval;
                const SysCallReg* regs = syscall_event->event_data.syscall.syscall_args.args;
                rv.as_i64 = _real_syscall(syscall_event->event_data.syscall.syscall_args.number,
                                          regs[0].as_u64, regs[1].as_u64, regs[2].as_u64,
                                          regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                // Make the requested syscall ourselves and return the result
                // to Shadow.
                const SysCallReg* regs = res.event_data.syscall.syscall_args.args;
                long syscall_rv = _real_syscall(res.event_data.syscall.syscall_args.number,
                                                regs[0].as_u64, regs[1].as_u64, regs[2].as_u64,
                                                regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                // Recover the true syscall return value from errno in the case
                // of an error.
                if (syscall_rv == -1) {
                    syscall_rv = -errno;
                }
                ShimEvent syscall_complete_event = {
                    .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                    .event_data.syscall_complete.retval.as_i64 = syscall_rv,
                };
                shimevent_sendEventToShadow(ipc_blk.p, &syscall_complete_event);
                break;
            }
            case SHD_SHIM_EVENT_CLONE_REQ:
                shim_shmemHandleClone(&res);
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            case SHD_SHIM_EVENT_CLONE_STRING_REQ:
                shim_shmemHandleCloneString(&res);
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            case SHD_SHIM_EVENT_WRITE_REQ:
                shim_shmemHandleWrite(&res);
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            case SHD_SHIM_EVENT_SHMEM_COMPLETE: shim_shmemNotifyComplete(ipc_blk.p); break;
            default: {
                error("Got unexpected event %d", res.event_id);
                abort();
            }
        }
    }
}

static long _vshadow_syscall(long n, va_list args) {
    shim_disableInterposition();
    ShimEvent e = {
        .event_id = SHD_SHIM_EVENT_SYSCALL,
        .event_data.syscall.syscall_args.number = n,
    };
    SysCallReg* regs = e.event_data.syscall.syscall_args.args;
    for (int i = 0; i < 6; ++i) {
        regs[i].as_u64 = va_arg(args, uint64_t);
    }
    long rv = shadow_retval_to_errno(_shadow_syscall_event(&e).as_i64);
    shim_enableInterposition();
    return rv;
}

long syscall(long n, ...) {
    shim_ensure_init();
    // Ensure that subsequent stack frames are on a different page than any
    // local variables passed through to the syscall. This ensures that even
    // if any of the syscall arguments are pointers, and those pointers cause
    // shadow to remap the pages containing those pointers, the shim-side stack
    // frames doing that work won't get their memory remapped out from under
    // them.
    void* padding = alloca(sysconf(_SC_PAGE_SIZE));

    // Ensure that the compiler doesn't optimize away `padding`.
    __asm__ __volatile__("" ::"m"(padding));

    va_list(args);
    va_start(args, n);
    long rv;
    if (shim_interpositionEnabled()) {
        debug("Making interposed syscall %ld", n);
        rv = _vshadow_syscall(n, args);
    } else {
        debug("Making real syscall %ld", n);
        rv = _vreal_syscall(n, args);
    }
    va_end(args);
    return rv;
}