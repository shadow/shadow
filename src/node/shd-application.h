/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#ifndef SHD_APPLICATION_H_
#define SHD_APPLICATION_H_

#include "shadow.h"

typedef struct _Application Application;

Application* application_new(GQuark pluginID, gchar* pluginPath,
		SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void application_free(Application* application);

void application_start(Application* application);
void application_stop(Application* application);
gboolean application_isRunning(Application* application);

void application_notify(Application* application);
void application_callback(Application* application, CallbackFunc userCallback,
		gpointer userData, gpointer userArgument, guint millisecondsDelay);

#endif /* SHD_APPLICATION_H_ */
