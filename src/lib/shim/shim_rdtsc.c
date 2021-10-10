/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/ucontext.h>

#include "lib/logger/logger.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_sys.h"
#include "lib/tsc/tsc.h"

static void _shim_rdtsc_handle_sigsegv(int sig, siginfo_t* info, void* voidUcontext) {
    trace("Trapped sigsegv");
    static bool tsc_initd = false;
    static Tsc tsc;
    if (!tsc_initd) {
        trace("Initializing tsc");
        uint64_t hz;
        if (sscanf(getenv("SHADOW_TSC_HZ"), "%" PRIu64, &hz) != 1) {
            panic("Couldn't parse SHADOW_TSC_HZ %s", getenv("SHADOW_TSC_HZ"));
        }
        tsc = Tsc_create(hz);
        tsc_initd = true;
    }

    ucontext_t* ctx = (ucontext_t*)(voidUcontext);
    greg_t* regs = ctx->uc_mcontext.gregs;

    unsigned char* insn = (unsigned char*)regs[REG_RIP];
    if (isRdtsc(insn)) {
        trace("Emulating rdtsc");
        uint64_t rax, rdx;
        uint64_t rip = regs[REG_RIP];
        Tsc_emulateRdtsc(&tsc, &rax, &rdx, &rip, shim_sys_get_simtime_nanos());
        regs[REG_RDX] = rdx;
        regs[REG_RAX] = rax;
        regs[REG_RIP] = rip;
        return;
    }
    if (isRdtscp(insn)) {
        trace("Emulating rdtscp");
        uint64_t rax, rdx, rcx;
        uint64_t rip = regs[REG_RIP];
        Tsc_emulateRdtscp(&tsc, &rax, &rdx, &rcx, &rip, shim_sys_get_simtime_nanos());
        regs[REG_RDX] = rdx;
        regs[REG_RAX] = rax;
        regs[REG_RCX] = rcx;
        regs[REG_RIP] = rip;
        return;
    }
    error("Unhandled sigsegv");

    // We don't have the "normal" segv signal handler to fall back on, but the
    // sigabrt handler typically does the same thing - dump core and exit with a
    // failure.
    raise(SIGABRT);
}

void shim_rdtsc_init() {
    // Force a SEGV on any rdtsc or rdtscp instruction.
    if (prctl(PR_SET_TSC, PR_TSC_SIGSEGV) < 0) {
        panic("pctl: %s", strerror(errno));
    }

    // Install our own handler to emulate.
    if (sigaction(SIGSEGV,
                  &(struct sigaction){
                      .sa_sigaction = _shim_rdtsc_handle_sigsegv,
                      // SA_NODEFER: Allow recursive signal handling, to handle a syscall
                      // being made during the handling of another. For example, we need this
                      // to properly handle the case that we end up logging from the syscall
                      // handler, and the IO syscalls themselves are trapped.
                      // SA_SIGINFO: Required because we're specifying sa_sigaction.
                      .sa_flags = SA_NODEFER | SA_SIGINFO,
                  },
                  NULL) < 0) {
        panic("sigaction: %s", strerror(errno));
    }
}
