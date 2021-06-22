#include <alloca.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include "main/host/syscall/kernel_types.h"
#include "main/shmem/shmem_allocator.h"
#include "lib/shim/ipc.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_event.h"
#include "lib/shim/shim_shmem.h"
#include "lib/shim/shim_syscall.h"
#include "lib/logger/logger.h"

// Never inline, so that the seccomp filter can reliably whitelist a syscall from
// this function.
// TODO: Drop if/when we whitelist using /proc/self/maps
long __attribute__((noinline)) shadow_vreal_raw_syscall(long n, va_list args) {
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
    return rv;
}

// Handle to the real syscall function, initialized once at load-time for
// thread-safety.
static long _shadow_real_raw_syscall(long n, ...) {
    va_list args;
    va_start(args, n);
    long rv = shadow_vreal_raw_syscall(n, args);
    va_end(args);
    return rv;
}

// Only called from asm, so need to tell compiler not to discard.
__attribute__((used)) static SysCallReg _shadow_raw_syscall_event(const ShimEvent* syscall_event) {

    ShMemBlock ipc_blk = shim_thisThreadEventIPCBlk();

    trace("sending syscall %ld event on %p", syscall_event->event_data.syscall.syscall_args.number,
          ipc_blk.p);

    shimevent_sendEventToShadow(ipc_blk.p, syscall_event);
    SysCallReg rv = {0};

    // By default we assume Shadow will return quickly, and so should spin
    // rather than letting the OS block this thread.
    bool spin = true;
    while (true) {
        trace("waiting for event on %p", ipc_blk.p);
        ShimEvent res = {0};
        shimevent_recvEventFromShadow(ipc_blk.p, &res, spin);
        trace("got response of type %d on %p", res.event_id, ipc_blk.p);
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
                shim_syscall_set_simtime_nanos(res.event_data.syscall_complete.simulation_nanos);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL_DO_NATIVE: {
                // Make the original syscall ourselves and use the result.
                SysCallReg rv = res.event_data.syscall_complete.retval;
                const SysCallReg* regs = syscall_event->event_data.syscall.syscall_args.args;
                rv.as_i64 = _shadow_real_raw_syscall(
                    syscall_event->event_data.syscall.syscall_args.number, regs[0].as_u64,
                    regs[1].as_u64, regs[2].as_u64, regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                // Make the requested syscall ourselves and return the result
                // to Shadow.
                const SysCallReg* regs = res.event_data.syscall.syscall_args.args;
                long syscall_rv = _shadow_real_raw_syscall(
                    res.event_data.syscall.syscall_args.number, regs[0].as_u64, regs[1].as_u64,
                    regs[2].as_u64, regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
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
            default: {
                panic("Got unexpected event %d", res.event_id);
                abort();
            }
        }
    }
}

static long _vshadow_raw_syscall(long n, va_list args) {
    shim_disableInterposition();
    ShimEvent e = {
        .event_id = SHD_SHIM_EVENT_SYSCALL,
        .event_data.syscall.syscall_args.number = n,
    };
    SysCallReg* regs = e.event_data.syscall.syscall_args.args;
    for (int i = 0; i < 6; ++i) {
        regs[i].as_u64 = va_arg(args, uint64_t);
    }

    /* On the first syscall, Shadow will remap the stack region of memory. In
     * preload-mode, this process is actively involved in that operation, with
     * several messages back and forth. To do that processing, we must use a
     * stack region *other* than the one being remapped. We handle this by
     * switching to a small dedicated stack, making the call, and then switching
     * back.
     */

    // Needs to be big enough to run signal handlers in case Shadow delivers a
    // non-fatal signal.
    // TODO: Allocate dynamically and only do this until the MemoryManager is
    // initialized.
    static __thread char new_stack[4096 * 10];

    void* old_stack;
    SysCallReg retval;
    asm volatile("movq %[EVENT], %%rdi\n"     /* set up syscall arg */
                 "movq %%rsp, %%rbx\n" /* save stack pointer to a callee-save register*/
                 "movq %[NEW_STACK], %%rsp\n" /* switch stack */
                 "callq _shadow_raw_syscall_event\n"
                 "movq %%rbx, %%rsp\n" /* restore stack pointer */
                 "movq %%rax, %[RETVAL]\n"    /* save return value */
                 :                            /* outputs */
                 [ RETVAL ] "=rm"(retval)
                 : /* inputs */
                 /* Must be a register, since a memory operand would be relative to the stack.
                    Note that we need to point to the *top* of the stack. */
                 [ NEW_STACK ] "r"(&new_stack[sizeof(new_stack)]), [ EVENT ] "rm"(&e)
                 : /* clobbers */
                 "memory",
                 /* used to save rsp */
                 "rbx",
                 /* All caller-saved registers not already used above */
                 "rax", "rdi", "rdx", "rcx", "rsi", "r8", "r9", "r10", "r11");

    shim_enableInterposition();
    return retval.as_i64;
}

// emulate a syscall *instruction*. i.e. doesn't rewrite the return val to errno.
long shadow_vraw_syscall(long n, va_list args) {
    shim_ensure_init();

    long rv;

    if (shim_use_syscall_handler() && shim_syscall(n, &rv, args)) {
        // No inter-process syscall needed, we handled it on the shim side! :)
        trace("Handled syscall %ld from the shim; we avoided inter-process overhead.", n);
        // rv was already set
    } else if (shim_interpositionEnabled()) {
        // The syscall is made using the shmem IPC channel.
        trace("Making syscall %ld indirectly; we ask shadow to handle it using the shmem IPC "
              "channel.",
              n);
        rv = _vshadow_raw_syscall(n, args);
    } else {
        // The syscall is made directly; ptrace will get the syscall signal.
        trace("Making syscall %ld directly; we expect ptrace will interpose it, or it will be "
              "handled natively by the kernel.",
              n);
        rv = shadow_vreal_raw_syscall(n, args);
    }

    return rv;
}

long shadow_raw_syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shadow_vraw_syscall(n, args);
    va_end(args);
    return rv;
}

// Make sure we don't call any syscalls ourselves after this function is called, otherwise
// the errno that we set here could get overwritten before we return to the plugin.
static long shadow_retval_to_errno(long retval) {
    // Linux reserves -1 through -4095 for errors. See
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
    if (retval <= -1 && retval >= -4095) {
        errno = (int)-retval;
        return -1;
    }
    return retval;
}

long syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shadow_vraw_syscall(n, args);
    va_end(args);
    return shadow_retval_to_errno(rv);
}