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

#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/timer.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

Process* process_new(Host* host, guint processID, SimulationTime startTime,
                     SimulationTime stopTime, InterposeMethod interposeMethod,
                     const gchar* hostName, const gchar* pluginName,
                     const gchar* pluginPath, const gchar* pluginSymbol,
                     gchar** envv, gchar** argv);
void process_ref(Process* proc);
void process_unref(Process* proc);

void process_schedule(Process* proc, gpointer nothing);
void process_continue(Process* proc, Thread* thread);
void process_stop(Process* proc);
void process_detachPlugin(gpointer procptr, gpointer nothing);

const char* process_getWorkingDir(Process* proc);

// Adds a new thread to the process and schedules it to run.
// Intended for use by `clone`.
void process_addThread(Process* proc, Thread* thread);

// In some cases a running thread processes an action that will bring down the
// entire process. Calling this tells the Process to clean up other threads
// without trying to run them again, since otherwise the OS may kill the other
// thread tasks while we're in the middle of trying to execute them, which can
// be difficult to recover from cleanly.
void process_markAsExiting(Process* proc);

gboolean process_isRunning(Process* proc);

/* Returns the name of the process from an internal buffer.
 * The returned pointer will become invalid when the process
 * is freed and therefore should not be persistently stored
 * outside of the process module. */
const gchar* process_getName(Process* proc);

/* Returns the name of the plugin from an internal buffer.
 * The returned pointer will become invalid when the process
 * is freed and therefore should not be persistently stored
 * outside of the process module. */
const gchar* process_getPluginName(Process* proc);

/* Returns the processID that was assigned to us in process_new */
guint process_getProcessID(Process* proc);

/* Returns the native tid of the thread with the given virtual PID and TID.
 * Although the process knows its own virtualPID already, giving it as a param
 * here allows us to control which of the PIF and TID get matched:
 * - If virtualPID is 0, then we only check for matching TIDs.
 * - If virtualTID is 0, then we return the TID of the main thread if the virutal PIDs match.
 * - If we don't find a matching thread, return 0. */
pid_t process_findNativeTID(Process* proc, pid_t virtualPID, pid_t virtualTID);

/* Handle all of the descriptors owned by this process. Deregistering a CompatDescriptor returns
 * an owned reference to that CompatDescriptor, and you must drop it manually when finished. */
int process_registerCompatDescriptor(Process* proc, CompatDescriptor* compatDesc);
CompatDescriptor* process_deregisterCompatDescriptor(Process* proc, int handle);
CompatDescriptor* process_getRegisteredCompatDescriptor(Process* proc, int handle);

/* Handle only the legacy descriptors owned by this process. Unlike the deregister method for the
 * CompatDescriptor, you do not need to manually unref the LegacyDescriptor as it's done
 * automatically. */
int process_registerLegacyDescriptor(Process* proc, LegacyDescriptor* desc);
void process_deregisterLegacyDescriptor(Process* proc, LegacyDescriptor* desc);
LegacyDescriptor* process_getRegisteredLegacyDescriptor(Process* proc, int handle);

// Convert a virtual ptr in the plugin address space to a globally unique physical ptr
PluginPhysicalPtr process_getPhysicalAddress(Process* proc, PluginVirtualPtr vPtr);

// Copy `n` bytes from `src` to `dst`. Returns 0 on success or EFAULT if any of
// the specified range couldn't be accessed.
int process_readPtr(Process* proc, Thread* thread, void* dst, PluginVirtualPtr src, size_t n);

// Copy `n` bytes from `src` to `dst`. Returns 0 on success or EFAULT if any of
// the specified range couldn't be accessed. The write is flushed immediately.
int process_writePtr(Process* proc, Thread* thread, PluginVirtualPtr dst, void* src, size_t n);

// Make the data at plugin_src available in shadow's address space.
//
// The returned pointer is read-only, and is automatically invalidated when the
// plugin runs again.
const void* process_getReadablePtr(Process* proc, Thread* thread, PluginPtr plugin_src, size_t n);

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getWriteablePtr(Process* proc, Thread* thread, PluginPtr plugin_src, size_t n);

// Returns a writeable pointer corresponding to the specified src. Use when
// the data at the given address needs to be both read and written.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getMutablePtr(Process* proc, Thread* thread, PluginPtr plugin_src, size_t n);

// Flushes and invalidates all previously returned readable/writable plugin
// pointers, as if returning control to the plugin. This can be useful in
// conjunction with `thread_nativeSyscall` operations that touch memory.
void process_flushPtrs(Process* proc, Thread* thread);

MemoryManager* process_getMemoryManager(Process* proc);
void process_setMemoryManager(Process* proc, MemoryManager* memoryManager);

// Returns the interpose method used by this process.
InterposeMethod process_getInterposeMethod(Process* proc);

// A wrapper around GLib's `g_shell_parse_argv()` that doesn't use GLib types. The returned
// pointers must be freed using `process_parseArgStrFree()`.
bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error);
// Free all data allocated by `process_parseArgStr()`.
void process_parseArgStrFree(char** argv, char* error);

#endif /* SHD_PROCESS_H_ */
