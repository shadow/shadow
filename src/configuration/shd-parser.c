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

#include "shadow.h"

/*
 * each action is given a priority so they are created in the correct order.
 * that way when e.g. a node needs a link to its network, the network
 * already exists, etc.
 */

struct _Parser {
	GMarkupParseContext* context;

	GMarkupParser parser;

	GMarkupParser nodeSubParser;
	CreateNodesAction* currentNodeAction;
	gint nChildApplications;

	GQueue* actions;
	MAGIC_DECLARE;
};

static void _parser_addAction(Parser* parser, Action* action) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(action);
	g_queue_insert_sorted(parser->actions, action, action_compare, NULL);
}

static GError* _parser_handleCDFAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	GString* id = NULL;
	GString* path = NULL;
	guint64 center = 0;
	guint64 width = 0;
	guint64 tail = 0;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if(!id && !g_ascii_strcasecmp(name, "id")) {
			id = g_string_new(value);
		} else if (!path && !g_ascii_strcasecmp(name, "path")) {
			path = g_string_new(value);
		} else if (!center && !g_ascii_strcasecmp(name, "center")) {
			center = g_ascii_strtoull(value, NULL, 10);
		} else if (!width && !g_ascii_strcasecmp(name, "width")) {
			width  = g_ascii_strtoull(value, NULL, 10);
		} else if (!tail && !g_ascii_strcasecmp(name, "tail")) {
			tail = g_ascii_strtoull(value, NULL, 10);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'cdf' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && (!id || (!path && !center))) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'cdf' requires attributes 'id' and either 'path' or 'center'");
	}

	if(!error) {
		/* no error, either load or generate a cdf
		 * if a path is given, we ignore the other attributes
		 */
		if(path) {
			Action* a = (Action*) loadcdf_new(id, path);
			a->priority = 1;
			_parser_addAction(parser, a);
		} else {
			Action* a = (Action*) generatecdf_new(id, center, width, tail);
			a->priority = 1;
			_parser_addAction(parser, a);
		}
	}

	/* clean up */
	if(path) {
		g_string_free(path, TRUE);
	}
	if(id) {
		g_string_free(id, TRUE);
	}

	return error;
}

static GError* _parser_handleClusterAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	GString* id = NULL;
	guint64 bandwidthdown = 0;
	guint64 bandwidthup = 0;
	gdouble packetloss = 0;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if(!id && !g_ascii_strcasecmp(name, "id")) {
			id = g_string_new(value);
		} else if (!bandwidthdown && !g_ascii_strcasecmp(name, "bandwidthdown")) {
			bandwidthdown = g_ascii_strtoull(value, NULL, 10);
		} else if (!bandwidthup && !g_ascii_strcasecmp(name, "bandwidthup")) {
			bandwidthup  = g_ascii_strtoull(value, NULL, 10);
		} else if (!packetloss && !g_ascii_strcasecmp(name, "packetloss")) {
			packetloss = g_ascii_strtod(value, NULL);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'cluster' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && (!id || !bandwidthdown || !bandwidthup)) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'cluster' requires attributes 'bandwidthdown' 'bandwidthup'");
	}

	if(!error) {
		/* no error, create the action */
		Action* a = (Action*) createnetwork_new(id, bandwidthdown, bandwidthup, packetloss);
		a->priority = 2;
		_parser_addAction(parser, a);
	}

	/* clean up */
	if(id) {
		g_string_free(id, TRUE);
	}

	return error;
}

static GError* _parser_handlePluginAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	GString* id = NULL;
	GString* path = NULL;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if(!id && !g_ascii_strcasecmp(name, "id")) {
			id = g_string_new(value);
		} else if (!path && !g_ascii_strcasecmp(name, "path")) {
			path = g_string_new(value);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'plugin' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && (!id || !path)) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'plugin' requires attributes 'id' 'path'");
	}

	if(!error) {
		/* no error, create the action */
		Action* a = (Action*) loadplugin_new(id, path);
		a->priority = 0;
		_parser_addAction(parser, a);
	}

	/* clean up */
	if(id) {
		g_string_free(id, TRUE);
	}
	if(path) {
		g_string_free(path, TRUE);
	}

	return error;
}

static GError* _parser_handleNodeAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	GString* id = NULL;
	GString* ip = NULL;
	GString* cluster = NULL;
	GString* loglevel = NULL;
	GString* heartbeatloglevel = NULL;
	GString* logpcap = NULL;
	GString* pcapdir = NULL;
	guint64 bandwidthdown = 0;
	guint64 bandwidthup = 0;
	guint64 heartbeatfrequency = 0;
	guint64 cpufrequency = 0;
	guint64 socketReceiveBufferSize = 0;
	guint64 socketSendBufferSize = 0;
	guint64 interfaceReceiveBufferLength = 0;
	/* if there is no quantity value, default should be 1 (allows a value of 0 to be explicity set) */
	guint64 quantity = 1;
	gboolean quantityIsSet = FALSE;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if(!id && !g_ascii_strcasecmp(name, "id")) {
			id = g_string_new(value);
		} else if (!ip && !g_ascii_strcasecmp(name, "ip")) {
			ip = g_string_new(value);
		} else if (!cluster && !g_ascii_strcasecmp(name, "cluster")) {
			cluster = g_string_new(value);
		} else if (!loglevel && !g_ascii_strcasecmp(name, "loglevel")) {
			loglevel = g_string_new(value);
		} else if (!heartbeatloglevel && !g_ascii_strcasecmp(name, "heartbeatloglevel")) {
			heartbeatloglevel = g_string_new(value);
		} else if (!logpcap && !g_ascii_strcasecmp(name, "logpcap")) {
			logpcap = g_string_new(value);
		} else if (!pcapdir && !g_ascii_strcasecmp(name, "pcapdir")) {
			pcapdir = g_string_new(value);
		} else if (!quantityIsSet && !g_ascii_strcasecmp(name, "quantity")) {
			quantity = g_ascii_strtoull(value, NULL, 10);
			quantityIsSet = TRUE;
		} else if (!bandwidthdown && !g_ascii_strcasecmp(name, "bandwidthdown")) {
			bandwidthdown  = g_ascii_strtoull(value, NULL, 10);
		} else if (!bandwidthup && !g_ascii_strcasecmp(name, "bandwidthup")) {
			bandwidthup = g_ascii_strtoull(value, NULL, 10);
		} else if (!heartbeatfrequency && !g_ascii_strcasecmp(name, "heartbeatfrequency")) {
			heartbeatfrequency = g_ascii_strtoull(value, NULL, 10);
		} else if (!cpufrequency && !g_ascii_strcasecmp(name, "cpufrequency")) {
			cpufrequency = g_ascii_strtoull(value, NULL, 10);
		} else if (!socketReceiveBufferSize && !g_ascii_strcasecmp(name, "socketrecvbuffer")) {
			socketReceiveBufferSize = g_ascii_strtoull(value, NULL, 10);
		} else if (!socketSendBufferSize && !g_ascii_strcasecmp(name, "socketsendbuffer")) {
			socketSendBufferSize = g_ascii_strtoull(value, NULL, 10);
		} else if (!interfaceReceiveBufferLength && !g_ascii_strcasecmp(name, "interfacebuffer")) {
			interfaceReceiveBufferLength = g_ascii_strtoull(value, NULL, 10);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'node' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && !id) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'node' requires attributes 'id'");
	}

	if(!error) {
		/* no error, create the action */
		Action* a = (Action*) createnodes_new(id, cluster,
				bandwidthdown, bandwidthup, quantity, cpufrequency,
				heartbeatfrequency, heartbeatloglevel, loglevel, logpcap, pcapdir,
				socketReceiveBufferSize, socketSendBufferSize, interfaceReceiveBufferLength);
		a->priority = 5;
		_parser_addAction(parser, a);

		/* save the parent so child applications can reference it */
		g_assert(!parser->currentNodeAction);
		parser->currentNodeAction = (CreateNodesAction*)a;
	}

	/* clean up */
	if(id) {
		g_string_free(id, TRUE);
	}
	if(ip) {
		g_string_free(ip, TRUE);
	}
	if(cluster) {
		g_string_free(cluster, TRUE);
	}
	if(loglevel) {
		g_string_free(loglevel, TRUE);
	}
	if(heartbeatloglevel) {
		g_string_free(heartbeatloglevel, TRUE);
	}
	if(logpcap) {
		g_string_free(logpcap, TRUE);
	}
	if(pcapdir) {
		g_string_free(pcapdir, TRUE);
	}

	return error;
}

static GError* _parser_handleKillAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	guint64 time = 0;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if (!time && !g_ascii_strcasecmp(name, "time")) {
			time = g_ascii_strtoull(value, NULL, 10);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'kill' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && !time) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'kill' requires attributes 'time'");
	}

	if(!error) {
		/* no error, create the action */
		Action* a = (Action*) killengine_new((SimulationTime) time);
		a->priority = 6;
		_parser_addAction(parser, a);
	}

	/* nothing to clean up */

	return error;
}

static GError* _parser_handleLinkAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	GString* clusters = NULL;
	guint64 latency = 0;
	guint64 jitter = 0;
	gdouble packetloss = 0.0;
	guint64 latencymin = 0;
	guint64 latencyQ1 = 0;
	guint64 latencymean = 0;
	guint64 latencyQ3 = 0;
	guint64 latencymax = 0;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if(!clusters && !g_ascii_strcasecmp(name, "clusters")) {
			clusters = g_string_new(value);
		} else if (!latency && !g_ascii_strcasecmp(name, "latency")) {
			latency = g_ascii_strtoull(value, NULL, 10);
		} else if (!jitter && !g_ascii_strcasecmp(name, "jitter")) {
			jitter = g_ascii_strtoull(value, NULL, 10);
		} else if (!latencymin && !g_ascii_strcasecmp(name, "latencymin")) {
			latencymin = g_ascii_strtoull(value, NULL, 10);
		} else if (!latencyQ1 && !g_ascii_strcasecmp(name, "latencyQ1")) {
			latencyQ1 = g_ascii_strtoull(value, NULL, 10);
		} else if (!latencymean && !g_ascii_strcasecmp(name, "latencymean")) {
			latencymean = g_ascii_strtoull(value, NULL, 10);
		} else if (!latencyQ3 && !g_ascii_strcasecmp(name, "latencyQ3")) {
			latencyQ3 = g_ascii_strtoull(value, NULL, 10);
		} else if (!latencymax && !g_ascii_strcasecmp(name, "latencymax")) {
			latencymax = g_ascii_strtoull(value, NULL, 10);
		} else if (!packetloss && !g_ascii_strcasecmp(name, "packetloss")) {
			packetloss = g_ascii_strtod(value, NULL);
		} else if (!g_ascii_strcasecmp(name, "id")) {
			/* TODO: ignore for now */
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'link' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && (!clusters || !latency)) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'link' requires attributes 'clusters' 'latency'");
	}

	GString* srcCluster = NULL;
	GString* destCluster = NULL;

	if(!error) {
		/* parse clusters string like "ABCD DCBA" into separate IDs */
		gchar** tokens = g_strsplit(clusters->str, " ", 2);
		g_assert(g_strrstr(tokens[1], " ") == NULL);

		srcCluster = g_string_new(tokens[0]);
		destCluster = g_string_new(tokens[1]);

		g_strfreev(tokens);

		/* no error, create the action */
		Action*  a = (Action*) connectnetwork_new(srcCluster, destCluster,
				latency, jitter, packetloss,
				latencymin, latencyQ1, latencymean, latencyQ3, latencymax);
		a->priority = 3;
		_parser_addAction(parser, a);
	}

	/* clean up */
	if(clusters) {
		g_string_free(clusters, TRUE);
	}
	if(srcCluster) {
		g_string_free(srcCluster, TRUE);
	}
	if(destCluster) {
		g_string_free(destCluster, TRUE);
	}

	return error;
}

static GError* _parser_handleApplicationAttributes(Parser* parser, const gchar** attributeNames, const gchar** attributeValues) {
	GString* plugin = NULL;
	GString* arguments = NULL;
	guint64 starttime = 0;
	guint64 stoptime = 0;

	GError* error = NULL;

	const gchar **nameCursor = attributeNames;
	const gchar **valueCursor = attributeValues;

	/* check the attributes */
	while (!error && *nameCursor) {
		const gchar* name = *nameCursor;
		const gchar* value = *valueCursor;

		debug("found attribute '%s=%s'", name, value);

		if(!plugin && !g_ascii_strcasecmp(name, "plugin")) {
			plugin = g_string_new(value);
		} else if (!arguments && !g_ascii_strcasecmp(name, "arguments")) {
			arguments = g_string_new(value);
		} else if (!starttime && !g_ascii_strcasecmp(name, "starttime")) {
			starttime = g_ascii_strtoull(value, NULL, 10);
		} else if (!starttime && !g_ascii_strcasecmp(name, "time")) { /* TODO deprecate 'time' */
			starttime = g_ascii_strtoull(value, NULL, 10);
		} else if (!stoptime && !g_ascii_strcasecmp(name, "stoptime")) {
			stoptime = g_ascii_strtoull(value, NULL, 10);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ATTRIBUTE,
							"unknown 'application' attribute '%s'", name);
		}

		nameCursor++;
		valueCursor++;
	}

	/* validate the values */
	if(!error && (!plugin || !arguments || !starttime)) {
		error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_MISSING_ATTRIBUTE,
				"element 'application' requires attributes 'plugin' 'arguments' 'starttime'");
	}

	if(!error) {
		/* no error, application configs get added to the node creation event
		 * in order to handle nodes with quantity > 1 */
		g_assert(parser->currentNodeAction);
		createnodes_addApplication(parser->currentNodeAction, plugin, arguments, starttime, stoptime);

		(parser->nChildApplications)++;
	}

	/* clean up */
	if(plugin) {
		g_string_free(plugin, TRUE);
	}
	if(arguments) {
		g_string_free(arguments, TRUE);
	}

	return error;
}

static void _parser_handleNodeChildStartElement(GMarkupParseContext* context,
		const gchar* elementName, const gchar** attributeNames,
		const gchar** attributeValues, gpointer userData, GError** error) {
	Parser* parser = (Parser*) userData;
	MAGIC_ASSERT(parser);
	g_assert(context && error);

	debug("found 'node' child starting element '%s'", elementName);

	/* check for cluster child-level elements */
	if (!g_ascii_strcasecmp(elementName, "application")) {
		*error = _parser_handleApplicationAttributes(parser, attributeNames, attributeValues);
	} else {
		*error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				"unknown 'node' child starting element '%s'", elementName);
	}
}

static void _parser_handleNodeChildEndElement(GMarkupParseContext* context,
		const gchar* elementName, gpointer userData, GError** error) {
	Parser* parser = (Parser*) userData;
	MAGIC_ASSERT(parser);
	g_assert(context && error);

	debug("found 'node' child ending element '%s'", elementName);

	/* check for cluster child-level elements */
	if (!(!g_ascii_strcasecmp(elementName, "application"))) {
		*error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				"unknown 'node' child ending element '%s'", elementName);
	}
}

static void _parser_handleRootStartElement(GMarkupParseContext* context,
		const gchar* elementName, const gchar** attributeNames,
		const gchar** attributeValues, gpointer userData, GError** error) {
	Parser* parser = (Parser*) userData;
	MAGIC_ASSERT(parser);
	g_assert(context && error);

	debug("found start element '%s'", elementName);

	/* check for root-level elements */
	if (!g_ascii_strcasecmp(elementName, "cdf")) {
		*error = _parser_handleCDFAttributes(parser, attributeNames, attributeValues);
	} else if (!g_ascii_strcasecmp(elementName, "cluster")) {
		*error = _parser_handleClusterAttributes(parser, attributeNames, attributeValues);
	} else if (!g_ascii_strcasecmp(elementName, "link")) {
		*error = _parser_handleLinkAttributes(parser, attributeNames, attributeValues);
	} else if (!g_ascii_strcasecmp(elementName, "plugin")) {
		*error = _parser_handlePluginAttributes(parser, attributeNames, attributeValues);
	} else if (!g_ascii_strcasecmp(elementName, "node")) {
		*error = _parser_handleNodeAttributes(parser, attributeNames, attributeValues);
		/* handle internal elements in a sub parser */
		g_markup_parse_context_push(context, &(parser->nodeSubParser), parser);
	} else if (!g_ascii_strcasecmp(elementName, "kill")) {
		*error = _parser_handleKillAttributes(parser, attributeNames, attributeValues);
	} else if (!g_ascii_strcasecmp(elementName, "hosts") ||
			!g_ascii_strcasecmp(elementName, "topology")) {
		/* do nothing, this is a root element */
	} else {
		*error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
				"unknown 'root' child starting element '%s'", elementName);
	}
}

static void _parser_handleRootEndElement(GMarkupParseContext* context,
		const gchar* elementName, gpointer userData, GError** error) {
	Parser* parser = (Parser*) userData;
	MAGIC_ASSERT(parser);
	g_assert(context && error);

	debug("found end element '%s'", elementName);

	/* check for root-level elements */
	if (!g_ascii_strcasecmp(elementName, "node")) {
		/* validate children */
		if (parser->nChildApplications <= 0) {
			*error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_EMPTY,
					"element 'node' requires at least 1 child 'application'");
		}
		g_markup_parse_context_pop(context);

		/* reset child cache */
		parser->nChildApplications = 0;
		if (parser->currentNodeAction) {
			/* this is in the actions queue and will get free'd later */
			parser->currentNodeAction = NULL;
		}
	} else {
		if(!(!g_ascii_strcasecmp(elementName, "cluster") ||
				!g_ascii_strcasecmp(elementName, "link") ||
				!g_ascii_strcasecmp(elementName, "plugin") ||
				!g_ascii_strcasecmp(elementName, "cdf") ||
				!g_ascii_strcasecmp(elementName, "kill") ||
				!g_ascii_strcasecmp(elementName, "hosts") ||
				!g_ascii_strcasecmp(elementName, "topology"))) {
			*error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
							"unknown 'root' child ending element '%s'", elementName);
		}
	}
}

/* public interface */

Parser* parser_new() {
	Parser* parser = g_new0(Parser, 1);
	MAGIC_INIT(parser);

	/* we handle the start_element and end_element callbacks, but ignore
	 * text, passthrough (comments), and errors
	 * handle both hosts and topology files.
	 */

	/* main root parser */
	parser->parser.start_element = &_parser_handleRootStartElement;
	parser->parser.end_element = &_parser_handleRootEndElement;
	parser->context = g_markup_parse_context_new(&(parser->parser), 0, parser, NULL);

	/* sub parsers, without their own context */
	parser->nodeSubParser.start_element = &_parser_handleNodeChildStartElement;
	parser->nodeSubParser.end_element = &_parser_handleNodeChildEndElement;

	return parser;
}

gboolean parser_parseContents(Parser* parser, gchar* contents, gsize length, GQueue* actions) {

	/* parse the contents, collecting actions. we store a pointer
	 * to it in parser so we have access while parsing elements. */
	parser->actions = actions;
	GError *error = NULL;
	gboolean success = g_markup_parse_context_parse(parser->context, contents, (gssize) length, &error);
	parser->actions = NULL;

	/* check for success in parsing and validating the XML */
	if(success && !error) {
		return TRUE;
	} else {
		/* some kind of error occurred, check the parser */
		g_assert(error);
		error("g_markup_parse_context_parse: Shadow XML parsing error %i: %s",
				error->code, error->message);
		g_error_free(error);

		return FALSE;
	}
}

gboolean parser_parseFile(Parser* parser, GString* filename, GQueue* actions) {
	MAGIC_ASSERT(parser);
	g_assert(filename && actions);

	gchar* content;
	gsize length;
	GError *error = NULL;

	/* get the xml file */
	gboolean success = g_file_get_contents(filename->str, &content, &length, &error);

	/* check for success */
	if (!success) {
		error("g_file_get_contents: %s", error->message);
		g_error_free(error);
		return FALSE;
	}

	/* do the actual parsing */
	debug("attempting to parse XML file '%s'", filename->str);
	gboolean result = parser_parseContents(parser, content, length, actions);
	debug("finished parsing XML file '%s'", filename->str);

	g_free(content);

	return result;
}

void parser_free(Parser* parser) {
	MAGIC_ASSERT(parser);

	/* cleanup */
	g_markup_parse_context_free(parser->context);

	MAGIC_CLEAR(parser);
	g_free(parser);
}
