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

Configuration* configuration_new(gint argc, gchar* argv[]) {
	/* get memory */
	Configuration* c = g_new0(Configuration, 1);
	MAGIC_INIT(c);

	const gchar* required_parameters = "input.xml ...";
	gint nRequiredXMLFiles = 1;

	c->context = g_option_context_new(required_parameters);
	g_option_context_set_summary(c->context, "Shadow - run real applications over simulated networks");
	g_option_context_set_description(c->context, "Shadow description");

	/* set defaults */
	c->nWorkerThreads = 0;
	c->minRunAhead = 10;
	c->printSoftwareVersion = 0;
	c->initialTCPWindow = 10;
	c->interfaceBufferSize = 1024000;
	c->interfaceBatchTime = 10;
	c->randomSeed = 1;
	c->cpuThreshold = 1000;
	c->heartbeatInterval = 60;

	/* set options to change defaults for the main group */
	c->mainOptionGroup = g_option_group_new("main", "Application Options", "Various application related options", NULL, NULL);
	const GOptionEntry mainEntries[] = {
	  { "log-level", 'l', 0, G_OPTION_ARG_STRING, &(c->logLevelInput), "Log LEVEL above which to filter messages (error < critical < warning < message < info < debug) [message]", "LEVEL" },
	  { "stat-log-level", 'g', 0, G_OPTION_ARG_STRING, &(c->heartbeatLogLevelInput), "Log LEVEL at which to print node statistics [info]", "LEVEL" },
	  { "stat-interval", 'h', 0, G_OPTION_ARG_INT, &(c->heartbeatInterval), "Log node statistics every N seconds [60]", "N" },
	  { "seed", 's', 0, G_OPTION_ARG_INT, &(c->randomSeed), "Initialize randomness for each thread using seed N [1]", "N" },
	  { "workers", 'w', 0, G_OPTION_ARG_INT, &(c->nWorkerThreads), "Use N worker threads [0]", "N" },
	  { "version", 'v', 0, G_OPTION_ARG_NONE, &(c->printSoftwareVersion), "Print software version and exit", NULL },
	  { NULL },
	};

	g_option_group_add_entries(c->mainOptionGroup, mainEntries);
	g_option_context_set_main_group(c->context, c->mainOptionGroup);

	/* now fill in the network option group */
	c->networkOptionGroup = g_option_group_new("network", "System Options", "Various system and network related options", NULL, NULL);
	const GOptionEntry networkEntries[] =
	{
	  { "cpu-threshold", 0, 0, G_OPTION_ARG_INT, &(c->cpuThreshold), "TIME delay threshold after which the CPU becomes blocked, in microseconds (negative value to disable CPU delays) [1000]", "TIME" },
	  { "interface-batch", 0, 0, G_OPTION_ARG_INT, &(c->interfaceBatchTime), "Batch TIME for network interface sends and receives, in milliseconds [10]", "TIME" },
	  { "interface-buffer", 0, 0, G_OPTION_ARG_INT, &(c->interfaceBufferSize), "Size of the network interface receive buffer, in bytes [1024000]", "N" },
	  { "runahead", 0, 0, G_OPTION_ARG_INT, &(c->minRunAhead), "Minimum allowed TIME workers may run ahead when sending events between nodes, in milliseconds [10]", "TIME" },
	  { "tcp-windows", 0, 0, G_OPTION_ARG_INT, &(c->initialTCPWindow), "Initialize the TCP send, receive, and congestion windows to N packets [10]", "N" },
	  { NULL },
	};

	g_option_group_add_entries(c->networkOptionGroup, networkEntries);
	g_option_context_add_group(c->context, c->networkOptionGroup);

	/* now fill in the default plug-in examples option group */
	c->pluginsOptionGroup = g_option_group_new("plug-ins", "Plug-in Examples", "Run example simulations with built-in plug-ins", NULL, NULL);
	const GOptionEntry pluginEntries[] =
	{
	  { "echo", 0, 0, G_OPTION_ARG_NONE, &(c->runEchoExample), "Run basic echo simulation", NULL },
	  { "file", 0, 0, G_OPTION_ARG_NONE, &(c->runFileExample), "Run basic HTTP file transfer simulation", NULL },
	  { "torrent", 0, 0, G_OPTION_ARG_NONE, &(c->runTorrentExample), "Run basic Torrent transfer simulation", NULL },
	  { NULL },
	};

	g_option_group_add_entries(c->pluginsOptionGroup, pluginEntries);
	g_option_context_add_group(c->context, c->pluginsOptionGroup);

	/* parse args */
	GError *error = NULL;
	if (!g_option_context_parse(c->context, &argc, &argv, &error)) {
		g_printerr("** %s **\n", error->message);
		g_printerr("%s", g_option_context_get_help(c->context, TRUE, NULL));
		configuration_free(c);
		return NULL;
	}

	/* make sure we have the required arguments. program name is first arg.
	 * printing the software version requires no other args. running a
	 * plug-in example also requires no other args. */
	if(!(c->printSoftwareVersion) && !(c->runEchoExample) && !(c->runFileExample) && !(c->runTorrentExample) &&
			(argc < nRequiredXMLFiles + 1)) {
		g_printerr("** Please provide the required parameters **\n");
		g_printerr("%s", g_option_context_get_help(c->context, TRUE, NULL));
		configuration_free(c);
		return NULL;
	}

	if(c->nWorkerThreads < 0) {
		c->nWorkerThreads = 0;
	}
	if(c->logLevelInput == NULL) {
		c->logLevelInput = g_strdup("message");
	}
	if(c->heartbeatLogLevelInput == NULL) {
		c->heartbeatLogLevelInput = g_strdup("info");
	}
	if(c->heartbeatInterval < 1) {
		c->heartbeatInterval = 1;
	}
	if(c->initialTCPWindow < 1) {
		c->initialTCPWindow = 1;
	}
	if(c->interfaceBufferSize < CONFIG_MTU) {
		c->interfaceBufferSize = CONFIG_MTU;
	}
	if(c->interfaceBatchTime < 0) {
		c->interfaceBatchTime = 0;
	}
	c->interfaceBatchTime *= SIMTIME_ONE_MILLISECOND;
	if(c->interfaceBatchTime == 0) {
		/* we require at least 1 nanosecond b/c of time granularity */
		c->interfaceBatchTime = 1;
	}

	c->inputXMLFilenames = g_queue_new();
	for(gint i = 1; i < argc; i++) {
		GString* filename = g_string_new(argv[i]);
		g_queue_push_tail(c->inputXMLFilenames, filename);
	}

	return c;
}

void configuration_free(Configuration* config) {
	MAGIC_ASSERT(config);

	if(config->inputXMLFilenames) {
		g_queue_free(config->inputXMLFilenames);
	}
	g_free(config->logLevelInput);
	g_free(config->heartbeatLogLevelInput);

	/* groups are freed with the context */
	g_option_context_free(config->context);

	MAGIC_CLEAR(config);
	g_free(config);
}

GLogLevelFlags _configuration_getLevel(Configuration* config, const gchar* input) {
	MAGIC_ASSERT(config);
	if (g_ascii_strcasecmp(input, "error") == 0) {
		return G_LOG_LEVEL_ERROR;
	} else if (g_ascii_strcasecmp(input, "critical") == 0) {
		return G_LOG_LEVEL_CRITICAL;
	} else if (g_ascii_strcasecmp(input, "warning") == 0) {
		return G_LOG_LEVEL_WARNING;
	} else if (g_ascii_strcasecmp(input, "message") == 0) {
		return G_LOG_LEVEL_MESSAGE;
	} else if (g_ascii_strcasecmp(input, "info") == 0) {
		return G_LOG_LEVEL_INFO;
	} else if (g_ascii_strcasecmp(input, "debug") == 0) {
		return G_LOG_LEVEL_DEBUG;
	} else {
		return G_LOG_LEVEL_MESSAGE;
	}
}

GLogLevelFlags configuration_getLogLevel(Configuration* config) {
	MAGIC_ASSERT(config);
	const gchar* l = (const gchar*) config->logLevelInput;
	return _configuration_getLevel(config, l);
}

GLogLevelFlags configuration_getHeartbeatLogLevel(Configuration* config) {
	MAGIC_ASSERT(config);
	const gchar* l = (const gchar*) config->heartbeatLogLevelInput;
	return _configuration_getLevel(config, l);
}
