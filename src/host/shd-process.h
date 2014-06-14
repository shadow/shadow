/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_APPLICATION_H_
#define SHD_APPLICATION_H_

#include "shadow.h"

typedef struct _Process Process;

Process* process_new(GQuark pluginID, SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void process_free(Process* proc);

void process_start(Process* proc);
void process_stop(Process* proc);
gboolean process_isRunning(Process* proc);

void process_notify(Process* proc);
void process_callback(Process* proc, CallbackFunc userCallback,
		gpointer userData, gpointer userArgument, guint millisecondsDelay);

#endif /* SHD_APPLICATION_H_ */
