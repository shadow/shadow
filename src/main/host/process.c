/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/process.h"

#include <errno.h>
#include <fcntl.h>
#include <features.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <ifaddrs.h>
#include <limits.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include "glib/gprintf.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/managed_thread.h"
#include "main/host/process.h"
#include "main/host/shimipc.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/utility.h"

struct _Process {
    /* Pointer to the RustProcess that owns this Process */
    const RustProcess* rustProcess;

    MAGIC_DECLARE;
};

const ShimShmemProcess* process_getSharedMem(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getSharedMem(proc->rustProcess);
}

const gchar* process_getName(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getName(proc->rustProcess);
}

StraceFmtMode process_straceLoggingMode(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_straceLoggingMode(proc->rustProcess);
}

int process_getStraceFd(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_straceFd(proc->rustProcess);
}

const gchar* process_getPluginName(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getPluginName(proc->rustProcess);
}

const char* process_getWorkingDir(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getWorkingDir(proc->rustProcess);
}

pid_t process_getProcessID(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getProcessID(proc->rustProcess);
}

pid_t process_getNativePid(const Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getNativePid(proc->rustProcess);
}

void process_addThread(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);
    _process_addThread(proc->rustProcess, thread);
}

Thread* process_getThread(Process* proc, pid_t virtualTID) {
    return _process_getThread(proc->rustProcess, virtualTID);
}

void process_markAsExiting(Process* proc) {
    MAGIC_ASSERT(proc);
    _process_markAsExiting(proc->rustProcess);
}

void process_continue(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);
    _process_continue(proc->rustProcess, thread);
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);
    _process_stop(proc->rustProcess);
}

gboolean process_hasStarted(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_hasStarted(proc->rustProcess);
}

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_isRunning(proc->rustProcess);
}

void process_initSiginfoForAlarm(siginfo_t* siginfo, int overrun) {
    *siginfo = (siginfo_t){
        .si_signo = SIGALRM,
        .si_code = SI_TIMER,
        .si_overrun = overrun,
    };
}

Process* process_new(const RustProcess* rustProcess, const Host* host, pid_t processID,
                     bool pause_for_debugging) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->rustProcess = rustProcess;

    worker_count_allocation(Process);

    return proc;
}

void process_setRustProcess(Process* proc, const RustProcess* rproc) {
    MAGIC_ASSERT(proc);
    utility_alwaysAssert(proc->rustProcess == NULL);
    proc->rustProcess = rproc;
}

const RustProcess* process_getRustProcess(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_alwaysAssert(proc->rustProcess);
    return proc->rustProcess;
}

void process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    // FIXME: call to _process_terminate removed.
    // We can't call it here, since the Rust Process inside the RustProcess (RootededRefCell<Process>)
    // has been extracted, invalidating proc->rustProcess.
    //
    // We *shouldn't* need to call _process_terminate here, since Host::free_all_applications
    // already explicitly stops all processes before freeing them, but once the relevant code is
    // all in Rust we should ensure the process is terminated in our Drop implementation.

    worker_count_deallocation(Process);

    MAGIC_CLEAR(proc);
    g_free(proc);
}

HostId process_getHostId(const Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getHostId(proc->rustProcess);
}

PluginPhysicalPtr process_getPhysicalAddress(Process* proc, PluginVirtualPtr vPtr) {
    MAGIC_ASSERT(proc);
    return _process_getPhysicalAddress(proc->rustProcess, vPtr);
}

int process_readPtr(Process* proc, void* dst, PluginVirtualPtr src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_readPtr(proc->rustProcess, dst, src, n);
}

int process_writePtr(Process* proc, PluginVirtualPtr dst, const void* src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_writePtr(proc->rustProcess, dst, src, n);
}

const void* process_getReadablePtr(Process* proc, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_getReadablePtr(proc->rustProcess, plugin_src, n);
}

int process_getReadableString(Process* proc, PluginPtr plugin_src, size_t n, const char** out_str,
                              size_t* out_strlen) {
    MAGIC_ASSERT(proc);
    return _process_getReadableString(proc->rustProcess, plugin_src, n, out_str, out_strlen);
}

ssize_t process_readString(Process* proc, char* str, PluginVirtualPtr src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_readString(proc->rustProcess, src, str, n);
}

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getWriteablePtr(Process* proc, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_getWriteablePtr(proc->rustProcess, plugin_src, n);
}

// Returns a writeable pointer corresponding to the specified src. Use when
// the data at the given address needs to be both read and written.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getMutablePtr(Process* proc, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_getMutablePtr(proc->rustProcess, plugin_src, n);
}

// Flushes and invalidates all previously returned readable/writeable plugin
// pointers, as if returning control to the plugin. This can be useful in
// conjunction with `thread_nativeSyscall` operations that touch memory.
int process_flushPtrs(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_flushPtrs(proc->rustProcess);
}

void process_freePtrsWithoutFlushing(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_freePtrsWithoutFlushing(proc->rustProcess);
}

// ******************************************************
// Handle the descriptors owned by this process
// ******************************************************

bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error) {
    GError* gError = NULL;

    bool rv = !!g_shell_parse_argv(commandLine, argc, argv, &gError);
    if (!rv && gError != NULL && gError->message != NULL && error != NULL) {
        *error = strdup(gError->message);
    }

    if (gError != NULL) {
        g_error_free(gError);
    }
    return rv;
}

void process_parseArgStrFree(char** argv, char* error) {
    if (argv != NULL) {
        g_strfreev(argv);
    }
    if (error != NULL) {
        g_free(error);
    }
}

void process_signal(Process* process, Thread* currentRunningThread, const siginfo_t* siginfo) {
    MAGIC_ASSERT(process);
    _process_signal(process->rustProcess, currentRunningThread, siginfo);
}

int process_getDumpable(Process* process) {
    return _process_getDumpable(process->rustProcess);
}

void process_setDumpable(Process* process, int dumpable) {
    _process_setDumpable(process->rustProcess, dumpable);
}
