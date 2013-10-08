/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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

Application* application_new(GQuark pluginID, const gchar* pluginPath,
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
		/* get arguments from the configured software */
		gchar** argv;
		gint argc = _application_getArguments(application, &argv);

		/* we will need to free each argument, copy argc in case they change it */
		gint n = argc;

		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		worker_setCurrentApplication(application);
		/* create our default state as we run in our assigned worker */
		application->state = plugin_newDefaultState(plugin);
		plugin_executeNew(plugin, application->state, argc, argv);
		worker_setCurrentApplication(NULL);

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
		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		worker_setCurrentApplication(application);
		plugin_executeNotify(plugin, application->state);
		worker_setCurrentApplication(NULL);
	}
}

static void _application_callbackTimerExpired(Application* application, ApplicationCallbackData* data) {
	MAGIC_ASSERT(application);
	utility_assert(data);

	if(application_isRunning(application)) {
		/* need to get thread-private plugin from current worker */
		Plugin* plugin = worker_getPlugin(application->pluginID, application->pluginPath);

		worker_setCurrentApplication(application);
		plugin_executeGeneric(plugin, application->state, data->callback, data->data, data->argument);
		worker_setCurrentApplication(NULL);
	}

	g_free(data);
}

void application_callback(Application* application, CallbackFunc userCallback,
		gpointer userData, gpointer userArgument, guint millisecondsDelay) {
	MAGIC_ASSERT(application);
	utility_assert(application_isRunning(application));

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
