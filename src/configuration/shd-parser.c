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
	"plugin", "cdf", "application", "node", "network", "link",
};

/* these MUST be synced with ParserAttributes in shd-parser.h */
static const gchar* ParserAttributeStrings[] = {
	"name", "path", "center", "width", "tail", "plugin", "arguments",
	"application", "bandwidthup", "bandwidthdown", "cpu", "quantity",
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
			values->bandwidthdown = g_ascii_strtoull(*value_cursor, NULL, 10);
		} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_BANDWIDTHUP]) == 0) {
			values->bandwidthup = g_ascii_strtoull(*value_cursor, NULL, 10);
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

	/* contains logic for building actions from topology file elements */
	if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_CDF]) == 0) {

		if(values->path) {
			/* if a path is given, we ignore the other attributes */
//			LoadCDFAction a = loadcdf_new(values->path)
		}

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NETWORK]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_LINK]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_PLUGIN]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_APPLICATION]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NODE]) == 0) {

	} else {
		warning("unrecognized element '%s' while parsing topology. ignoring.", element_name);
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
	parser->actions = NULL;

	return parser;
}

GSList* parser_parse(Parser* parser, GString* filename) {
	MAGIC_ASSERT(parser);
	g_assert(filename);

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

	/* parse the file, collecting actions */
	success = g_markup_parse_context_parse(parser->context, content, length, &error);

	debug("finished parsing XML file '%s'", filename->str);

	/* check for success */
	if (!success) {
		error("parser_parse: g_markup_parse_context_parse: %s\n", error->message);
		g_error_free(error);
		return FALSE;
	}

	g_free(content);

	return parser->actions;
}

void parser_free(Parser* parser) {
	MAGIC_ASSERT(parser);

	/* cleanup */
	g_markup_parse_context_free(parser->context);
	g_slist_free(parser->actions);

	MAGIC_CLEAR(parser);
	g_free(parser);
}
