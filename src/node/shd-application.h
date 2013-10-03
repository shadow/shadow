/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
