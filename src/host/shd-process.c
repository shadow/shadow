/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Process {
	GQuark programID;

	Program* prog;
	ProgramState state;
	Thread* mainThread;

	SimulationTime startTime;
	GString* arguments;
	MAGIC_DECLARE;
};

typedef struct _ProcessCallbackData ProcessCallbackData;
struct _ProcessCallbackData {
	CallbackFunc callback;
	gpointer data;
	gpointer argument;
};

Process* process_new(GQuark programID, SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
	Process* proc = g_new0(Process, 1);
	MAGIC_INIT(proc);

	proc->programID = programID;
	proc->startTime = startTime;
	proc->arguments = g_string_new(arguments);

	return proc;
}

void process_free(Process* proc) {
	MAGIC_ASSERT(proc);

	process_stop(proc);

	g_string_free(proc->arguments, TRUE);

	MAGIC_CLEAR(proc);
	g_free(proc);
}

static gint _process_getArguments(Process* proc, gchar** argvOut[]) {
	gchar* threadBuffer;

	gchar* argumentString = g_strdup(proc->arguments->str);
	GQueue *arguments = g_queue_new();

	/* first argument is the name of the program */
	const gchar* pluginName = g_quark_to_string(proc->programID);
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

gboolean process_isRunning(Process* proc) {
	MAGIC_ASSERT(proc);
	return proc->state ? TRUE : FALSE;
}

void process_start(Process* proc) {
	MAGIC_ASSERT(proc);

	/* dont do anything if we are already running */
	if(!process_isRunning(proc)) {
		/* need to get thread-private program from current worker */
        proc->prog = worker_getPrivateProgram(proc->programID);

        /* get arguments from the configured software */
		gchar** argv;
		gint argc = _process_getArguments(proc, &argv);

		/* we will need to free each argument, copy argc in case they change it */
		gint n = argc;

		proc->mainThread = thread_new();
		worker_setCurrentApplication(proc);

		/* create our default state as we run in our assigned worker */
		proc->state = program_newDefaultState(proc->prog);
		program_executeNew(proc->prog, proc->state, argc, argv);

		worker_setCurrentApplication(NULL);

		/* free the arguments */
		for(gint i = 0; i < n; i++) {
			g_free(argv[i]);
		}
		g_free(argv);
	}
}

void process_stop(Process* proc) {
	MAGIC_ASSERT(proc);

	/* we only have state if we are running */
	if(process_isRunning(proc)) {
		/* tell the plug-in module (user code) to free its data */
		program_executeFree(proc->prog, proc->state);

		/* free our copy of plug-in resources, and other application state */
		program_freeState(proc->prog, proc->state);
		proc->state = NULL;

		thread_free(proc->mainThread);
	}
}

void process_notify(Process* proc) {
	MAGIC_ASSERT(proc);

	/* only notify if we are running */
	if(process_isRunning(proc)) {
		worker_setCurrentApplication(proc);
		program_executeNotify(proc->prog, proc->state);
		worker_setCurrentApplication(NULL);
	}
}

static void _process_callbackTimerExpired(Process* proc, ProcessCallbackData* data) {
	MAGIC_ASSERT(proc);
	utility_assert(data);

	if(process_isRunning(proc)) {
		worker_setCurrentApplication(proc);
		program_executeGeneric(proc->prog, proc->state, data->callback, data->data, data->argument);
		worker_setCurrentApplication(NULL);
	}

	g_free(data);
}

void process_callback(Process* proc, CallbackFunc userCallback,
		gpointer userData, gpointer userArgument, guint millisecondsDelay) {
	MAGIC_ASSERT(proc);
	utility_assert(process_isRunning(proc));

	/* the application wants a callback. since we need it to happen in our
	 * application and plug-in context, we create a callback to our own
	 * function first, and then redirect and execute theirs
	 */

	ProcessCallbackData* data = g_new0(ProcessCallbackData, 1);
	data->callback = userCallback;
	data->data = userData;
	data->argument = userArgument;

	CallbackEvent* event = callback_new((CallbackFunc)_process_callbackTimerExpired, proc, data);
	SimulationTime nanos = SIMTIME_ONE_MILLISECOND * millisecondsDelay;

	/* callback to our own node */
	worker_scheduleEvent((Event*)event, nanos, 0);
}
