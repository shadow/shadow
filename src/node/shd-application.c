/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shadow.h"

struct _Application {
	GQuark pluginID;
	GString* pluginPath;
	PluginState state;

	SimulationTime startTime;
	GString* arguments;
	MAGIC_DECLARE;
};

typedef struct _ApplicationCallbackData ApplicationCallbackData;
struct _ApplicationCallbackData {
	CallbackFunc callback;
	gpointer data;
	gpointer argument;
};

Application* application_new(GQuark pluginID, gchar* pluginPath,
		SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
	Application* application = g_new0(Application, 1);
	MAGIC_INIT(application);

	application->pluginID = pluginID;
	application->pluginPath = g_string_new(pluginPath);
	application->startTime = startTime;
	application->arguments = g_string_new(arguments);

	return application;
}

void application_free(Application* application) {
	MAGIC_ASSERT(application);

	application_stop(application);

	g_string_free(application->pluginPath, TRUE);
	g_string_free(application->arguments, TRUE);

	MAGIC_CLEAR(application);
	g_free(application);
}

static gint _application_getArguments(Application* application, gchar** argvOut[]) {
	gchar* threadBuffer;

	gchar* argumentString = g_strdup(application->arguments->str);
	GQueue *arguments = g_queue_new();

	/* first argument is the name of the program */
	const gchar* pluginName = g_quark_to_string(application->pluginID);
	g_queue_push_tail(arguments, g_strdup(pluginName));

	/* parse the full argument string into separate strings */
	gchar* token = strtok_r(argumentString, " ", &threadBuffer);
	while(token != NULL) {
		gchar* argument = g_strdup((const gchar*) token);
		g_queue_push_tail(arguments, argument);
		token = strtok_r(NULL, " ", &threadBuffer);
	}

	/* setup for creating new plug-in, i.e. format into argc and argv */
	gint argc = g_queue_get_length(arguments);
	/* a pointer to an array that holds pointers */
	gchar** argv = g_new0(gchar*, argc);

	for(gint i = 0; i < argc; i++) {
		argv[i] = g_queue_pop_head(arguments);
	}

	/* cleanup */
	g_free(argumentString);
	g_queue_free(arguments);

	/* transfer to the caller - they must free argv and each element of it */
	*argvOut = argv;
	return argc;
}

gboolean application_isRunning(Application* application) {
	MAGIC_ASSERT(application);
	return application->state ? TRUE : FALSE;
}

void application_start(Application* application) {
	MAGIC_ASSERT(application);

	/* dont do anything if we are already running */
	if(!application_isRunning(application)) {
		Worker* worker = worker_getPrivate();

		/* get arguments from the configured software */
		gchar** argv;
		gint argc = _application_getArguments(application, &argv);

		/* we will need to free each argument, copy argc in case they change it */
		gint n = argc;

		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		worker->cached_application = application;
		/* create our default state as we run in our assigned worker */
		application->state = plugin_newDefaultState(plugin);
		plugin_executeNew(plugin, application->state, argc, argv);
		worker->cached_application = NULL;

		/* free the arguments */
		for(gint i = 0; i < n; i++) {
			g_free(argv[i]);
		}
		g_free(argv);
	}
}

void application_stop(Application* application) {
	MAGIC_ASSERT(application);

	/* we only have state if we are running */
	if(application_isRunning(application)) {
		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		/* tell the plug-in module (user code) to free its data */
		plugin_executeFree(plugin, application->state);

		/* free our copy of plug-in resources, and other application state */
		plugin_freeState(plugin, application->state);
		application->state = NULL;
	}
}

void application_notify(Application* application) {
	MAGIC_ASSERT(application);

	/* only notify if we are running */
	if(application_isRunning(application)) {
		Worker* worker = worker_getPrivate();

		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		worker->cached_application = application;
		plugin_executeNotify(plugin, application->state);
		worker->cached_application = NULL;
	}
}

static void _application_callbackTimerExpired(Application* application, ApplicationCallbackData* data) {
	MAGIC_ASSERT(application);
	g_assert(data);

	if(application_isRunning(application)) {
		Worker* worker = worker_getPrivate();

		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		worker->cached_application = application;
		plugin_executeGeneric(plugin, application->state, data->callback, data->data, data->argument);
		worker->cached_application = NULL;
	}

	g_free(data);
}

void application_callback(Application* application, CallbackFunc userCallback,
		gpointer userData, gpointer userArgument, guint millisecondsDelay) {
	MAGIC_ASSERT(application);
	g_assert(application_isRunning(application));

	/* the application wants a callback. since we need it to happen in our
	 * application and plug-in context, we create a callback to our own
	 * function first, and then redirect and execute theirs
	 */

	ApplicationCallbackData* data = g_new0(ApplicationCallbackData, 1);
	data->callback = userCallback;
	data->data = userData;
	data->argument = userArgument;

	CallbackEvent* event = callback_new((CallbackFunc)_application_callbackTimerExpired, application, data);
	SimulationTime nanos = SIMTIME_ONE_MILLISECOND * millisecondsDelay;

	/* callback to our own node */
	worker_scheduleEvent((Event*)event, nanos, 0);
}
