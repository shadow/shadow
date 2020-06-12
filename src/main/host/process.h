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

#include "main/core/support/definitions.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/timer.h"
#include "main/host/syscall_handler.h"
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

gboolean process_wantsNotify(Process* proc, gint epollfd);
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

/* Listen for the given status to occur on descriptor, or the timer to expire.
 * When either of those occur, the process calls thread_resume on
 * the given thread. */
void process_listenForStatus(Process* proc, Thread* thread, Timer* timeout,
                             Descriptor* descriptor, DescriptorStatus status);

int process_registerDescriptor(Process* proc, Descriptor* desc);
void process_deregisterDescriptor(Process* proc, Descriptor* desc);
Descriptor* process_getRegisteredDescriptor(Process* proc, int handle);

#endif /* SHD_PROCESS_H_ */
