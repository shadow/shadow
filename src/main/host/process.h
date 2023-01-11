/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROCESS_H_
#define SHD_PROCESS_H_

#include <dlfcn.h>
#include <fcntl.h>
#include <features.h>
#include <glib.h>
#include <ifaddrs.h>
#include <linux/sockios.h>
#include <malloc.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/file.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

typedef struct _Process Process;

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

Process* process_new(const RustProcess* rustProcess, const Host* host, pid_t processID,
                     bool pause_for_debugging);

// For use by the Rust Process.
void process_setRustProcess(Process* proc, const RustProcess* rproc);
const RustProcess* process_getRustProcess(Process* proc);

void process_free(Process* proc);

void process_continue(Process* proc, Thread* thread);
void process_stop(Process* proc);

const char* process_getWorkingDir(Process* proc);

// Adds a new thread to the process and schedules it to run.
// Intended for use by `clone`.
//
// Takes ownership of `thread`.
void process_addThread(Process* proc, Thread* thread);

Thread* process_getThread(Process* proc, pid_t virtualTID);

// In some cases a running thread processes an action that will bring down the
// entire process. Calling this tells the Process to clean up other threads
// without trying to run them again, since otherwise the OS may kill the other
// thread tasks while we're in the middle of trying to execute them, which can
// be difficult to recover from cleanly.
void process_markAsExiting(Process* proc);

gboolean process_hasStarted(Process* proc);
gboolean process_isRunning(Process* proc);

/* Returns the name of the process from an internal buffer.
 * The returned pointer will become invalid when the process
 * is freed and therefore should not be persistently stored
 * outside of the process module. */
const gchar* process_getName(Process* proc);

StraceFmtMode process_straceLoggingMode(Process* proc);
int process_getStraceFd(Process* proc);

/* Returns the name of the plugin from an internal buffer.
 * The returned pointer will become invalid when the process
 * is freed and therefore should not be persistently stored
 * outside of the process module. */
const gchar* process_getPluginName(Process* proc);

/* Returns the processID that was assigned to us in process_new */
pid_t process_getProcessID(Process* proc);

/* Returns the native pid of the process */
pid_t process_getNativePid(const Process* proc);

// Convert a virtual ptr in the plugin address space to a globally unique physical ptr
PluginPhysicalPtr process_getPhysicalAddress(Process* proc, PluginVirtualPtr vPtr);

// Copy `n` bytes from `src` to `dst`. Returns 0 on success or EFAULT if any of
// the specified range couldn't be accessed. Always succeeds with n==0.
int process_readPtr(Process* proc, void* dst, PluginVirtualPtr src, size_t n);

// Make the data starting at plugin_src, and extending until the first NULL
// byte, up at most `n` bytes, available in shadow's address space.
//
// * `str` must be non-NULL, and is set to point to the given string. It is
//   invalidated when the plugin runs again.
// * `strlen` may be NULL. If it isn't, is set to `strlen(str)`.
//
// Returns:
// 0 on success.
// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
// -EFAULT if the string extends beyond the accessible address space.
int process_getReadableString(Process* process, PluginPtr plugin_src, size_t n, const char** str,
                              size_t* strlen);

// Reads up to `n` bytes into `str`.
//
// Returns:
// strlen(str) on success.
// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
// -EFAULT if the string extends beyond the accessible address space.
ssize_t process_readString(Process* proc, char* str, PluginVirtualPtr src, size_t n);

// Copy `n` bytes from `src` to `dst`. Returns 0 on success or EFAULT if any of
// the specified range couldn't be accessed. The write is flushed immediately.
int process_writePtr(Process* proc, PluginVirtualPtr dst, const void* src, size_t n);

// Make the data at plugin_src available in shadow's address space.
//
// The returned pointer is read-only, and is automatically invalidated when the
// plugin runs again.
const void* process_getReadablePtr(Process* proc, PluginPtr plugin_src, size_t n);

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
//
// CAUTION: if the unspecified contents aren't overwritten, and the pointer
// isn't explicitly freed via `process_freePtrsWithoutFlushing`, those unspecified contents may
// be written back into process memory.
void* process_getWriteablePtr(Process* proc, PluginPtr plugin_src, size_t n);

// Returns a writeable pointer corresponding to the specified src. Use when
// the data at the given address needs to be both read and written.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getMutablePtr(Process* proc, PluginPtr plugin_src, size_t n);

// Flushes and invalidates all previously returned readable/writable plugin
// pointers, as if returning control to the plugin. This can be useful in
// conjunction with `thread_nativeSyscall` operations that touch memory, or
// to gracefully handle failed writes.
//
// Returns 0 on success or a positive errno on failure.
int process_flushPtrs(Process* proc) __attribute__((warn_unused_result));

// Frees all readable/writable plugin pointers. Unlike process_flushPtrs, any
// previously returned writable pointer is *not* written back. Useful
// if an uninitialized writable pointer was obtained via `process_getWriteablePtr`,
// and we end up not wanting to write anything after all (in particular, don't
// write back whatever garbage data was in the uninialized bueffer).
void process_freePtrsWithoutFlushing(Process* proc);

HostId process_getHostId(const Process* proc);

// A wrapper around GLib's `g_shell_parse_argv()` that doesn't use GLib types. The returned
// pointers must be freed using `process_parseArgStrFree()`.
bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error);
// Free all data allocated by `process_parseArgStr()`.
void process_parseArgStrFree(char** argv, char* error);

// Process state kept in memory shared with the managed process's shim.
const ShimShmemProcess* process_getSharedMem(Process* proc);

// Send the signal described in `siginfo` to `process`. `currentRunningThread`
// should be set if there is one (e.g. if this is being called from a syscall
// handler), and NULL otherwise (e.g. when called from a timer expiration event).
void process_signal(Process* process, Thread* currentRunningThread, const siginfo_t* siginfo);

// Process's "dumpable" state, as manipulated by the prctl operations
// PR_SET_DUMPABLE and PR_GET_DUMPABLE.
int process_getDumpable(Process* process);
void process_setDumpable(Process* process, int dumpable);

// Helper for the Rust Process. `siginfo_t` is difficult to initialize from Rust,
// due to opaque fields and macro magic in its C definition.
void process_initSiginfoForAlarm(siginfo_t* siginfo, int overrun);

#endif /* SHD_PROCESS_H_ */
