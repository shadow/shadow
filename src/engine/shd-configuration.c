/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

Configuration* configuration_new(gint argc, gchar* argv[]) {
	/* get memory */
	Configuration* c = g_new(Configuration, 1);
	MAGIC_INIT(c);

	/* set defaults */
	c->context = g_option_context_new(NULL);
	g_option_context_set_summary(c->context, "Shadow - run real applications over simulated networks");
	g_option_context_set_description(c->context, "Shadow description");

	c->nWorkerThreads = 0;
	c->minTimeJump = 10;
	c->printSoftwareVersion = 0;

	/* set options to change defaults */
	const GOptionEntry entries[] =
	{
	  { "threads", 't', 0, G_OPTION_ARG_INT, &(c->nWorkerThreads), "Use N worker threads", "N" },
	  { "jump-min", 'j', 0, G_OPTION_ARG_INT, &(c->minTimeJump), "Minimum allowed time jump when sending events between nodes, in milliseconds", "N" },
	  { "version", 'v', 0, G_OPTION_ARG_NONE, &(c->printSoftwareVersion), "Print software version and exit", NULL },
	  { NULL },
	};

	g_option_context_add_main_entries(c->context, entries, NULL);

	/* parse args */
	GError *error = NULL;
	if (!g_option_context_parse(c->context, &argc, &argv, &error)) {
		g_print("**%s**\n", error->message);
		g_print(g_option_context_get_help(c->context, TRUE, NULL));
		configuration_free(c);
		return NULL;
	}

	if(c->nWorkerThreads < 0) {
		c->nWorkerThreads = 0;
	}

	return c;
}

void configuration_free(Configuration* config) {
	MAGIC_ASSERT(config);

	g_option_context_free(config->context);

	MAGIC_CLEAR(config);
	g_free(config);
}
