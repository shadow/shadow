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

/* NOTE - these MUST be synced with ParserElements */
static const gchar* ParserElementStrings[] = {
	"plugin", "cdf", "software", "node", "cluster", "link", "kill",
};

/* NOTE - they MUST be synced with ParserElementStrings */
typedef enum {
	ELEMENT_PLUGIN,
	ELEMENT_CDF,
	ELEMENT_SOFTWARE,
	ELEMENT_NODE,
	ELEMENT_CLUSTER,
	ELEMENT_LINK,
	ELEMENT_KILL,
} ParserElements;

/* NOTE - these MUST be synced with ParserAttributes in */
static const gchar* ParserAttributeStrings[] = {
	"id", "path", "center", "width", "tail",
	"plugin", "software", "cluster", "clusters",
	"bandwidthdown", "bandwidthup", "latency", "jitter", "packetloss",
	"cpufrequency", "time", "quantity", "arguments",
};

/* NOTE - they MUST be synced with ParserAttributeStrings */
typedef enum {
	ATTRIBUTE_ID,
	ATTRIBUTE_PATH,
	ATTRIBUTE_CENTER,
	ATTRIBUTE_WIDTH,
	ATTRIBUTE_TAIL,
	ATTRIBUTE_PLUGIN,
	ATTRIBUTE_SOFTWARE,
	ATTRIBUTE_CLUSTER,
	ATTRIBUTE_CLUSTERS,
	ATTRIBUTE_BANDWIDTHDOWN,
	ATTRIBUTE_BANDWIDTHUP,
	ATTRIBUTE_LATENCY,
	ATTRIBUTE_JITTER,
	ATTRIBUTE_PACKETLOSS,
	ATTRIBUTE_CPUFREQUENCY,
	ATTRIBUTE_TIME,
	ATTRIBUTE_QUANTITY,
	ATTRIBUTE_ARGUMENTS,
} ParserAttributes;

struct _Parser {
	GMarkupParser parser;
	GMarkupParseContext* context;
	GQueue* actions;
	gboolean hasValidationError;
	MAGIC_DECLARE;
};

typedef struct _ParserValues ParserValues;

struct _ParserValues {
	/* represents a unique ID */
	GString* id;
	/* path to a file */
	GString* path;
	/* center of base of CDF - meaning dependent on what the CDF represents */
	guint64 center;
	/* width of base of CDF - meaning dependent on what the CDF represents */
	guint64 width;
	/* width of tail of CDF - meaning dependent on what the CDF represents */
	guint64 tail;
	/* holds the unique ID name of a plugin */
	GString* plugin;
	/* holds the unique ID name of software */
	GString* software;
	/* holds the unique ID name of cluster */
	GString* cluster;
	GString* linkedclusters;
	/* holds the bandwidth (KiB/s) */
	guint64 bandwidthup;
	/* holds the bandwidth (KiB/s) */
	guint64 bandwidthdown;
	/* holds the latency (milliseconds) */
	guint64 latency;
	/* holds the variation in latency (milliseconds) */
	guint64 jitter;
	/* fraction between 0 and 1 - liklihood that a packet gets dropped */
	gdouble packetloss;
	/* string of arguments that will be passed to the software */
	GString* arguments;
	/* time in seconds */
	guint64 time;
	guint64 quantity;
	guint64 cpufrequency;
	MAGIC_DECLARE;
};

static ParserValues* _parser_getValues(const gchar *element_name,
		const gchar **attribute_names, const gchar **attribute_values)
{
	const gchar **name_cursor = attribute_names;
	const gchar **value_cursor = attribute_values;

	ParserValues* values = g_new0(ParserValues, 1);
	MAGIC_INIT(values);

	/* loop through all the attributes, fill in values as we find them */
	while (*name_cursor) {
		debug("found attribute '%s=%s'", *name_cursor, *value_cursor);

		/* contains the actual logic for parsing attributes of topology files. */
		if (g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_SOFTWARE]) == 0) {
			values->software = g_string_new(*value_cursor);
		} else if (g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_ARGUMENTS]) == 0) {
			values->arguments = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_BANDWIDTHDOWN]) == 0) {
			values->bandwidthdown = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_BANDWIDTHUP]) == 0) {
			values->bandwidthup = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CENTER]) == 0) {
			values->center = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CLUSTER]) == 0) {
			values->cluster = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CLUSTERS]) == 0) {
			values->linkedclusters = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_LATENCY]) == 0) {
			values->latency = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_JITTER]) == 0) {
			values->jitter = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CPUFREQUENCY]) == 0) {
			values->cpufrequency = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_TIME]) == 0) {
			values->time = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_ID]) == 0) {
			values->id = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_PATH]) == 0) {
			values->path = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_PLUGIN]) == 0) {
			values->plugin = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_QUANTITY]) == 0) {
			values->quantity = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_PACKETLOSS]) == 0) {
			values->packetloss = g_ascii_strtod(*value_cursor, NULL);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_TAIL]) == 0) {
			values->tail = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_WIDTH]) == 0) {
			values->width = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else {
			warning("unrecognized attribute '%s' for element '%s' while parsing topology. ignoring.", *name_cursor, element_name);
		}

		name_cursor++;
		value_cursor++;
	}

	return values;
}

static void _parser_freeValues(ParserValues* values) {
	MAGIC_ASSERT(values);

	if(values->software)
		g_string_free(values->software, TRUE);
	if(values->arguments)
		g_string_free(values->arguments, TRUE);
	if(values->cluster)
		g_string_free(values->cluster, TRUE);
	if(values->linkedclusters)
		g_string_free(values->linkedclusters, TRUE);
	if(values->id)
		g_string_free(values->id, TRUE);
	if(values->path)
		g_string_free(values->path, TRUE);
	if(values->plugin)
		g_string_free(values->plugin, TRUE);

	MAGIC_CLEAR(values);
	g_free(values);
}

static gboolean _parser_validateCDF(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->id || (!values->path && !values->center)) {
		critical("element '%s' requires attributes '%s' and either '%s' or '%s'",
				ParserElementStrings[ELEMENT_CDF],
				ParserAttributeStrings[ATTRIBUTE_ID],
				ParserAttributeStrings[ATTRIBUTE_PATH],
				ParserAttributeStrings[ATTRIBUTE_CENTER]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateCluster(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->id || !values->bandwidthdown || !values->bandwidthup) {
		critical("element '%s' requires attributes '%s' '%s' '%s'",
				ParserElementStrings[ELEMENT_CLUSTER],
				ParserAttributeStrings[ATTRIBUTE_ID],
				ParserAttributeStrings[ATTRIBUTE_BANDWIDTHDOWN],
				ParserAttributeStrings[ATTRIBUTE_BANDWIDTHUP]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateLink(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->linkedclusters || !values->latency) {
		critical("element '%s' requires attributes '%s' '%s'",
				ParserElementStrings[ELEMENT_LINK],
				ParserAttributeStrings[ATTRIBUTE_CLUSTERS],
				ParserAttributeStrings[ATTRIBUTE_LATENCY]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validatePlugin(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->id || !values->path) {
		critical("element '%s' requires attributes '%s' '%s'",
				ParserElementStrings[ELEMENT_PLUGIN],
				ParserAttributeStrings[ATTRIBUTE_ID],
				ParserAttributeStrings[ATTRIBUTE_PATH]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateApplication(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->id || !values->plugin || !values->time || !values->arguments) {
		critical("element '%s' requires attributes '%s' '%s' '%s' '%s'",
				ParserElementStrings[ELEMENT_SOFTWARE],
				ParserAttributeStrings[ATTRIBUTE_ID],
				ParserAttributeStrings[ATTRIBUTE_PLUGIN],
				ParserAttributeStrings[ATTRIBUTE_TIME],
				ParserAttributeStrings[ATTRIBUTE_ARGUMENTS]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateNode(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->id || !values->software)
	{
		critical("element '%s' requires attributes '%s' '%s'",
				ParserElementStrings[ELEMENT_NODE],
				ParserAttributeStrings[ATTRIBUTE_ID],
				ParserAttributeStrings[ATTRIBUTE_SOFTWARE]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateKill(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->time)
	{
		critical("element '%s' requires attributes '%s'",
				ParserElementStrings[ELEMENT_KILL],
				ParserAttributeStrings[ATTRIBUTE_TIME]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static void _parser_handleElement(GMarkupParseContext *context,
		const gchar *element_name, const gchar **attribute_names,
		const gchar **attribute_values, gpointer user_data, GError **error) {
	g_assert(context);
	Parser* parser = user_data;
	MAGIC_ASSERT(parser);

	debug("found element '%s'", element_name);

	ParserValues* values = _parser_getValues(element_name, attribute_names, attribute_values);
	MAGIC_ASSERT(values);

	/*
	 * TODO: we should really check if attributes are set for an element
	 * that are invalid for that element and print out a warning.
	 * we might be able to do this easier in getValues()
	 */

	/* we hope to extract an action from the element */
	Action* a = NULL;

	/* each action is given a priority so they are created in the correct order.
	 * that way when e.g. a node needs a link to its network, the network
	 * already exists, etc.
	 */

	/* contains logic for building actions from topology file elements */
	if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_CDF]) == 0) {
		/* first check that we have all the types we need */
		 if(_parser_validateCDF(parser, values)) {
			/*
			 * now we either load or generate a cdf, depending on path
			 * if a path is given, we ignore the other attributes
			 */
			if(values->path) {
				a = (Action*) loadcdf_new(values->id, values->path);
			} else {
				a = (Action*) generatecdf_new(values->id, values->center,
						values->width, values->tail);
			}
			a->priority = 1;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_CLUSTER]) == 0) {
		if(_parser_validateCluster(parser, values)) {
			a = (Action*) createnetwork_new(values->id, values->bandwidthdown, values->bandwidthup);
			a->priority = 2;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_LINK]) == 0) {
		if(_parser_validateLink(parser, values)) {
			a = (Action*) connectnetwork_new(values->linkedclusters,
					values->latency, values->jitter, values->packetloss);
			a->priority = 3;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_PLUGIN]) == 0) {
		if(_parser_validatePlugin(parser, values)) {
			a = (Action*) loadplugin_new(values->id, values->path);
			a->priority = 0;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_SOFTWARE]) == 0) {
		if(_parser_validateApplication(parser, values)) {
			a = (Action*) createsoftware_new(values->id, values->plugin,
					values->arguments, values->time);
			a->priority = 4;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NODE]) == 0) {
		if(_parser_validateNode(parser, values)) {
			a = (Action*) createnodes_new(values->id, values->software, values->cluster,
					values->bandwidthdown, values->bandwidthup, values->quantity, values->cpufrequency);
			a->priority = 5;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_KILL]) == 0) {
		if(_parser_validateKill(parser, values)) {
			a = (Action*) killengine_new((SimulationTime) values->time);
			a->priority = 6;
		}
	} else {
		warning("unrecognized element '%s' while parsing topology. ignoring.", element_name);
	}

	/* keep track of our actions */
	if(a) {
		g_queue_insert_sorted(parser->actions, a, action_compare, NULL);
	}

	_parser_freeValues(values);
}

Parser* parser_new() {
	Parser* parser = g_new0(Parser, 1);
	MAGIC_INIT(parser);

	/* we handle everything in start_element callback, and ignore end_element,
	 * text, passthrough (comments), and errors
	 * handle both hosts and topology files.
	 */

	parser->parser.start_element = &_parser_handleElement;
	parser->context = g_markup_parse_context_new(&parser->parser, 0, parser, NULL);

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
	if(success && !parser->hasValidationError) {
		return TRUE;
	} else {
		/* some kind of error occurred, check the parser */
		if (!success) {
			error("g_markup_parse_context_parse: %s", error->message);
			g_error_free(error);
		}

		/* also check for validation */
		if(parser->hasValidationError) {
			critical("XML validation error");
		}

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
	message("finished parsing XML file '%s'", filename->str);

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
