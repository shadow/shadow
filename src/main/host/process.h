/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROCESS_H_
#define SHD_PROCESS_H_

#include <bits/stdint-intn.h>
#include <bits/types/clockid_t.h>
#include <bits/types/time_t.h>
#include <glib.h>
#include <stdarg.h>
#include <stdint.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <malloc.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/file.h>
#include <features.h>
#include <linux/sockios.h>
#include <pthread.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "main/core/support/definitions.h"
#include "main/host/shd-syscall-handler.h"

Process* process_new(SystemCallHandler* sys, guint processID,
        SimulationTime startTime, SimulationTime stopTime, const gchar* hostName,
        const gchar* pluginName, const gchar* pluginPath, const gchar* pluginSymbol,
        const gchar* preloadName, const gchar* preloadPath, gchar* arguments);
void process_ref(Process* proc);
void process_unref(Process* proc);

void process_schedule(Process* proc, gpointer nothing);
void process_continue(Process* proc);
void process_stop(Process* proc);

gboolean process_wantsNotify(Process* proc, gint epollfd);
gboolean process_isRunning(Process* proc);

#endif /* SHD_PROCESS_H_ */
