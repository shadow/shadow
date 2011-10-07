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

/* these MUST be synced with ParserElements in shd-parser.h */
static const gchar* ParserElementStrings[] = {
	"plugin", "cdf", "application", "node", "network", "link", "kill",
};

/* these MUST be synced with ParserAttributes in shd-parser.h */
static const gchar* ParserAttributeStrings[] = {
	"name", "path", "center", "width", "tail", "plugin", "arguments",
	"application", "time", "bandwidthup", "bandwidthdown", "cpu", "quantity",
	"network", "networka", "networkb",
	"latency", "latencyab", "latencyba",
	"reliability", "reliabilityab", "reliabilityba",
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
		if (g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_APPLICATION]) == 0) {
			values->application = g_string_new(*value_cursor);
		} else if (g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_ARGUMENTS]) == 0) {
			values->arguments = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_BANDWIDTHDOWN]) == 0) {
			values->bandwidthdown = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_BANDWIDTHUP]) == 0) {
			values->bandwidthup = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CENTER]) == 0) {
			values->center = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CPU]) == 0) {
			values->cpu = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_LATENCY]) == 0) {
			values->latency = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_LATENCYAB]) == 0) {
			values->latencyab = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_LATENCYBA]) == 0){
			values->latencyba = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_TIME]) == 0) {
			values->time = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_NAME]) == 0) {
			values->name = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_NETWORK]) == 0) {
			values->network = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_NETWORKA]) == 0) {
			values->networka = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_NETWORKB]) == 0) {
			values->networkb = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_PATH]) == 0) {
			values->path = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_PLUGIN]) == 0) {
			values->plugin = g_string_new(*value_cursor);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_QUANTITY]) == 0) {
			values->quantity = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_RELIABILITY]) == 0) {
			values->reliability = g_ascii_strtod(*value_cursor, NULL);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_RELIABILITYAB]) == 0) {
			values->reliabilityab = g_ascii_strtod(*value_cursor, NULL);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_RELIABILITYBA]) == 0) {
			values->reliabilityba = g_ascii_strtod(*value_cursor, NULL);
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

	if(values->application)
		g_string_free(values->application, TRUE);
	if(values->arguments)
		g_string_free(values->arguments, TRUE);
	if(values->bandwidthdown)
		g_string_free(values->bandwidthdown, TRUE);
	if(values->bandwidthup)
		g_string_free(values->bandwidthup, TRUE);
	if(values->cpu)
		g_string_free(values->cpu, TRUE);
	if(values->latency)
		g_string_free(values->latency, TRUE);
	if(values->latencyab)
		g_string_free(values->latencyab, TRUE);
	if(values->latencyba)
		g_string_free(values->latencyba, TRUE);
	if(values->name)
		g_string_free(values->name, TRUE);
	if(values->network)
		g_string_free(values->network, TRUE);
	if(values->networka)
		g_string_free(values->networka, TRUE);
	if(values->networkb)
		g_string_free(values->networkb, TRUE);
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

	if(!values->name || (!values->path && !values->center)) {
		critical("element '%s' requires attributes '%s' and either '%s' or '%s'",
				ParserElementStrings[ELEMENT_CDF],
				ParserAttributeStrings[ATTRIBUTE_NAME],
				ParserAttributeStrings[ATTRIBUTE_PATH],
				ParserAttributeStrings[ATTRIBUTE_CENTER]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateNetwork(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->name || !values->latency || !values->reliability) {
		critical("element '%s' requires attributes '%s' '%s' '%s'",
				ParserElementStrings[ELEMENT_NETWORK],
				ParserAttributeStrings[ATTRIBUTE_NAME],
				ParserAttributeStrings[ATTRIBUTE_LATENCY],
				ParserAttributeStrings[ATTRIBUTE_RELIABILITY]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateLink(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->networka || !values->networkb ||
			!values->latencyab || !values->latencyba ||
			!values->reliabilityab || !values->reliabilityba)
	{
		critical("element '%s' requires attributes '%s' '%s' '%s' '%s' '%s' '%s'",
				ParserElementStrings[ELEMENT_LINK],
				ParserAttributeStrings[ATTRIBUTE_NETWORKA],
				ParserAttributeStrings[ATTRIBUTE_NETWORKB],
				ParserAttributeStrings[ATTRIBUTE_LATENCYAB],
				ParserAttributeStrings[ATTRIBUTE_LATENCYBA],
				ParserAttributeStrings[ATTRIBUTE_RELIABILITYAB],
				ParserAttributeStrings[ATTRIBUTE_RELIABILITYBA]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validatePlugin(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->name || !values->path) {
		critical("element '%s' requires attributes '%s' '%s'",
				ParserElementStrings[ELEMENT_PLUGIN],
				ParserAttributeStrings[ATTRIBUTE_NAME],
				ParserAttributeStrings[ATTRIBUTE_PATH]);
		parser->hasValidationError = TRUE;
		return FALSE;
	}
	return TRUE;
}

static gboolean _parser_validateApplication(Parser* parser, ParserValues* values) {
	MAGIC_ASSERT(parser);
	MAGIC_ASSERT(values);

	if(!values->name || !values->plugin || !values->time || !values->arguments) {
		critical("element '%s' requires attributes '%s' '%s' '%s' '%s'",
				ParserElementStrings[ELEMENT_APPLICATION],
				ParserAttributeStrings[ATTRIBUTE_NAME],
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

	if(!values->name || !values->application || !values->network ||
			!values->bandwidthup || !values->bandwidthdown || !values->cpu)
	{
		critical("element '%s' requires attributes '%s' '%s' '%s' '%s' '%s' '%s'",
				ParserElementStrings[ELEMENT_NODE],
				ParserAttributeStrings[ATTRIBUTE_NAME],
				ParserAttributeStrings[ATTRIBUTE_APPLICATION],
				ParserAttributeStrings[ATTRIBUTE_NETWORK],
				ParserAttributeStrings[ATTRIBUTE_BANDWIDTHUP],
				ParserAttributeStrings[ATTRIBUTE_BANDWIDTHDOWN],
				ParserAttributeStrings[ATTRIBUTE_CPU]);
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
				ParserElementStrings[ELEMENT_NODE],
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
				a = (Action*) loadcdf_new(values->name, values->path);
			} else {
				a = (Action*) generatecdf_new(values->name, values->center,
						values->width, values->tail);
			}
			a->priority = 1;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NETWORK]) == 0) {
		if(_parser_validateNetwork(parser, values)) {
			a = (Action*) createnetwork_new(values->name, values->latency,
					values->reliability);
			a->priority = 2;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_LINK]) == 0) {
		if(_parser_validateLink(parser, values)) {
			a = (Action*) connectnetwork_new(values->networka, values->networkb,
					values->latencyab, values->reliabilityab, values->latencyba,
					values->reliabilityba);
			a->priority = 3;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_PLUGIN]) == 0) {
		if(_parser_validatePlugin(parser, values)) {
			a = (Action*) loadplugin_new(values->name, values->path);
			a->priority = 0;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_APPLICATION]) == 0) {
		if(_parser_validateApplication(parser, values)) {
			a = (Action*) createapplication_new(values->name, values->plugin,
					values->arguments, values->time);
			a->priority = 4;
		}
	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NODE]) == 0) {
		if(_parser_validateNode(parser, values)) {
			if(!values->quantity) {
				values->quantity = 1;
			}

			a = (Action*) createnodes_new(values->quantity, values->name,
					values->application, values->cpu, values->network,
					values->bandwidthup, values->bandwidthdown);
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

gboolean parser_parse(Parser* parser, GString* filename, GQueue* actions) {
	MAGIC_ASSERT(parser);
	g_assert(filename && actions);

	gchar* content;
	gsize length;
	GError *error = NULL;

	/* get the xml file */
	gboolean success = g_file_get_contents(filename->str, &content, &length, &error);

	/* check for success */
	if (!success) {
		error("parser_parse: g_file_get_contents: %s\n", error->message);
		g_error_free(error);
		return FALSE;
	}

	debug("attempting to parse XML file '%s'", filename->str);

	/* parse the file, collecting actions. we store a pointer
	 * to it in parser so we have access while parsing elements. */
	parser->actions = actions;
	success = g_markup_parse_context_parse(parser->context, content, length, &error);
	parser->actions = NULL;

	g_free(content);

	info("finished parsing XML file '%s'", filename->str);

	/* check for success in parsing and validating the XML */
	if(success && !parser->hasValidationError) {
		return TRUE;
	} else {
		/* some kind of error ocurred */
		g_queue_free(actions);

		/* check parse error */
		if (!success) {
			error("parser_parse: g_markup_parse_context_parse: %s\n", error->message);
			g_error_free(error);
		}

		/* also check for validation */
		if(parser->hasValidationError) {
			critical("parser_parse: XML validation error");
		}

		return FALSE;
	}
}

void parser_free(Parser* parser) {
	MAGIC_ASSERT(parser);

	/* cleanup */
	g_markup_parse_context_free(parser->context);

	MAGIC_CLEAR(parser);
	g_free(parser);
}
