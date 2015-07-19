/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_APPLICATION_H_
#define SHD_APPLICATION_H_

#include "shadow.h"

Process* process_new(GQuark pluginID, SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void process_ref(Process* proc);
void process_unref(Process* proc);

void process_start(Process* proc);
void process_continue(Process* proc);
void process_stop(Process* proc);

gboolean process_isRunning(Process* proc);
gboolean process_shouldInterpose(Process* proc);

gboolean process_addAtExitCallback(Process* proc, gpointer userCallback, gpointer userArgument,
        gboolean shouldPassArgument);

void process_beginControl(Process* proc);
void process_endControl(Process* proc);

#endif /* SHD_APPLICATION_H_ */
