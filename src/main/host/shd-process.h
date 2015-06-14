/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_APPLICATION_H_
#define SHD_APPLICATION_H_

#include "shadow.h"

Process* process_new(GQuark pluginID, SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void process_free(Process* proc);

void process_boot(Process* proc, gpointer userData);
gboolean process_isRunning(Process* proc);

void process_notify(Process* proc, Thread* thread);
void process_callback(Process* proc, TaskFunc userCallback,
        gpointer userData, gpointer userArgument, guint millisecondsDelay);

gboolean process_addAtExitCallback(Process* proc, gpointer userCallback, gpointer userArgument,
        gboolean shouldPassArgument);

#endif /* SHD_APPLICATION_H_ */
