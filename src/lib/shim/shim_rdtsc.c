/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_syscall.h"
#include "lib/shim/shim_tls.h"
#include "lib/tsc/tsc.h"

_Noreturn static void _shim_rdtsc_die_with_sigsegv() {
    shim_disableInterposition();
    if (sigaction(SIGSEGV, &(struct sigaction){.sa_handler = SIG_DFL}, NULL) != 0) {
        panic("sigaction: %s", strerror(errno));
    }
    raise(SIGSEGV);
    panic("Unexpectedly survived SIGSEGV");
}

static void _shim_rdtsc_handle_sigsegv(int sig, siginfo_t* info, void* voidUcontext) {
    shim_disableInterposition();

    // Detect recursion. Unfortunately there's still potential for infinite
    // recursion here if there's a SEGV in this handling itself. We could avoid
    // that by using SA_RESETHAND with `sigaction` to have the system reset the
    // signal disposition every time this handler is invoked, but that doesn't
    // work well with threads when `CLONE_SIGHAND` is in use. OTOH the infinite
    // recursion should still ultimately result in a SIGSEGV - just with an
    // annoyingly large stacktrace.
    //
    // Maybe something to revisit when we fully virtualize and implement signal
    // handling; e.g. we could always clear CLONE_SIGHAND in `clone` calls, so
    // that we can control signal dispositions independently for each thread
    // (while emulating the CLONE_SIGHAND behavior for the managed process
    // itself).
    static ShimTlsVar in_handler_storage = {0};
    bool* in_handler = shimtlsvar_ptr(&in_handler_storage, sizeof(*in_handler));
    if (*in_handler) {
        error("Recursive sigsegv");
        _shim_rdtsc_die_with_sigsegv();
    }
    *in_handler = true;

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
    } else if (isRdtscp(insn)) {
        trace("Emulating rdtscp");
        uint64_t rax, rdx, rcx;
        uint64_t rip = regs[REG_RIP];
        Tsc_emulateRdtscp(&tsc, &rax, &rdx, &rcx, &rip, shim_sys_get_simtime_nanos());
        regs[REG_RDX] = rdx;
        regs[REG_RAX] = rax;
        regs[REG_RCX] = rcx;
        regs[REG_RIP] = rip;
    } else {
        error("Unhandled sigsegv");
        _shim_rdtsc_die_with_sigsegv();
    }
    *in_handler = false;
    shim_enableInterposition();
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
                      // SA_NODEFER: Handle recursive SIGSEGVs, so that it can
                      // "rethrow" the SIGSEGV and in case of a bug in the
                      // handler.
                      // SA_SIGINFO: Required because we're specifying
                      // sa_sigaction.
                      .sa_flags = SA_SIGINFO | SA_NODEFER,
                  },
                  NULL) < 0) {
        panic("sigaction: %s", strerror(errno));
    }
}
