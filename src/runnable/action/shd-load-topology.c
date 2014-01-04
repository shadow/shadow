/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <unistd.h>

#include "shadow.h"
#include "shd-action-internal.h"

struct _LoadTopologyAction {
	Action super;
	GString* path;
	GString* text;
	MAGIC_DECLARE;
};

RunnableFunctionTable loadtopology_functions = {
	(RunnableRunFunc) loadtopology_run,
	(RunnableFreeFunc) loadtopology_free,
	MAGIC_VALUE
};

LoadTopologyAction* loadtopology_new(GString* path, GString* text) {
	utility_assert(path || text);
	LoadTopologyAction* action = g_new0(LoadTopologyAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadtopology_functions);

	if(path) {
		action->path = g_string_new(path->str);
	}
	if(text) {
		action->text = g_string_new(text->str);
	}

	return action;
}

void loadtopology_run(LoadTopologyAction* action) {
	MAGIC_ASSERT(action);

	Topology* topology = NULL;

	/* igraph wants a path to an graphml file, prefer a path over cdata */
	if(action->path) {
		topology = topology_new(action->path->str);
		if(!topology) {
			error("error loading topology file '%s'", action->path->str);
			return;
		}
	} else {
		/* turn the cdata text contents into a file */
		GString* templateBuffer = g_string_new("shadow-cdata-XXXXXX.graphml.xml");

		/* try to open the templated file, checking for errors */
		gchar* temporaryFilename = NULL;
		GError* error = NULL;
		gint openedFile = g_file_open_tmp(templateBuffer->str, &temporaryFilename, &error);
		if(openedFile < 0) {
			error("unable to open temporary file for cdata topology: %s", error->message);
			return;
		}

		/* cleanup */
		close(openedFile);
		g_string_free(templateBuffer, TRUE);
		error = NULL;

		/* copy the cdata to the new temporary file */
		if(!g_file_set_contents(temporaryFilename, action->text->str, (gssize)action->text->len, &error)) {
			error("unable to write cdata topology to '%s': %s", temporaryFilename, error->message);
			return;
		}

		topology = topology_new(temporaryFilename);
		g_unlink(temporaryFilename);
		g_free(temporaryFilename);

		if(!topology) {
			error("error loading topology cdata");
			return;
		}
	}

	utility_assert(topology);
	worker_setTopology(topology);
}

void loadtopology_free(LoadTopologyAction* action) {
	MAGIC_ASSERT(action);

	if(action->path) {
		g_string_free(action->path, TRUE);
	}
	if(action->text) {
		g_string_free(action->text, TRUE);
	}

	MAGIC_CLEAR(action);
	g_free(action);
}
