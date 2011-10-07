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

void handle_topology_element(GMarkupParseContext *context,
		const gchar *element_name, const gchar **attribute_names,
		const gchar **attribute_values, gpointer user_data, GError **error) {
	g_assert(context);
	Parser* parser = user_data;
	MAGIC_ASSERT(parser);

	/* contains the actual logic for parsing topology files. here we define the
	 * allowable elements and attribute name/value pairs
	 */
	const gchar **name_cursor = attribute_names;
	const gchar **value_cursor = attribute_values;

	debug("%s", element_name);

	if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_CDF]) == 0) {

		/* if we find a path, it overrides the other attributes */
		GString* name = NULL;
		GString* path = NULL;
		guint64 center = 0;
		guint64 width = 0;
		guint64 tail = 0;

		/* loop through all the attributes */
		while (*name_cursor) {

			debug("found %s=%s", *name_cursor, *value_cursor);

			if (g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_NAME]) == 0) {
				name = g_string_new(*value_cursor);
			} else if (g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_PATH]) == 0) {
				path = g_string_new(*value_cursor);
			} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_CENTER]) == 0) {
				center = g_ascii_strtoull(*value_cursor, NULL, 10);
			} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_WIDTH]) == 0) {
				width = g_ascii_strtoull(*value_cursor, NULL, 10);
			} else if(g_ascii_strcasecmp(*name_cursor, ParserAttributeStrings[ATTRIBUTE_TAIL]) == 0) {
				tail = g_ascii_strtoull(*value_cursor, NULL, 10);
			} else {
				warning("unrecognized attribute '%s' for element '%s' while parsing topology. ignoring.", *name_cursor, element_name);
			}

			name_cursor++;
			value_cursor++;
		}

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NETWORK]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_LINK]) == 0) {

	} else {
		warning("unrecognized element '%s' while parsing topology. ignoring.", element_name);
	}

}

void handle_host_element(GMarkupParseContext *context,
		const gchar *element_name, const gchar **attribute_names,
		const gchar **attribute_values, gpointer user_data, GError **error) {
	g_assert(context);
	Parser* parser = user_data;
	MAGIC_ASSERT(parser);

	/* contains the actual logic for parsing topology files. here we define the
	 * allowable elements and attribute name/value pairs
	 */
	const gchar **name_cursor = attribute_names;
	const gchar **value_cursor = attribute_values;

	debug("%s", element_name);

	if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_PLUGIN]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_CDF]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_APPLICATION]) == 0) {

	} else if(g_ascii_strcasecmp(element_name, ParserElementStrings[ELEMENT_NODE]) == 0) {

	} else {
		warning("unrecognized element '%s' while parsing hosts. ignoring.", element_name);
	}
}

Parser* parser_new() {
	Parser* parser = g_new(Parser, 1);
	MAGIC_INIT(parser);

	/* we handle everything in start_element callback, and ignore end_element,
	 * text, passthrough (comments), and errors
	 * handle both hosts and topology files.
	 */

	parser->hostParser.start_element = &handle_host_element;
	parser->hostContext = g_markup_parse_context_new(&parser->hostParser, 0, parser, NULL);
	parser->hostActions = NULL;

	parser->topologyParser.start_element = &handle_topology_element;
	parser->topologyContext = g_markup_parse_context_new(&parser->topologyParser, 0, parser, NULL);
	parser->topologyActions = NULL;

	return parser;
}

gboolean _parser_parse(Parser* parser, GString* filename,
		GMarkupParseContext* context)
{
	MAGIC_ASSERT(parser);
	g_assert(filename);

	gchar* content;
	gsize length;
	GError *error = NULL;

	/* get the xml file */
	gboolean success = g_file_get_contents(filename->str, &content, &length, &error);

	/* check for success */
	if (!success) {
		error("parser_parseTopology: g_file_get_contents: %s\n", error->message);
		g_error_free(error);
		return FALSE;
	}

	/* parse the file, collecting actions */
	success = g_markup_parse_context_parse(context, content, length, &error);

	/* check for success */
	if (!success) {
		error("_parser_parse: g_markup_parse_context_parse: %s\n", error->message);
		g_error_free(error);
		return FALSE;
	}

	g_free(content);

	return TRUE;
}

GSList* parser_parseTopology(Parser* parser, GString* filename) {
	MAGIC_ASSERT(parser);

	/* call internal parser */
	gboolean success = _parser_parse(parser, filename, parser->topologyContext);

	/* returned the parse actions if we have success */
	if(success) {
		return parser->topologyActions;
	} else {
		return NULL;
	}
}

GSList* parser_parseHosts(Parser* parser, GString* filename) {
	MAGIC_ASSERT(parser);

	/* call internal parser */
	gboolean success = _parser_parse(parser, filename, parser->hostContext);

	/* returned the parse actions if we have success */
	if(success) {
		return parser->hostActions;
	} else {
		return NULL;
	}
}

void parser_free(Parser* parser) {
	MAGIC_ASSERT(parser);

	/* cleanup */
	g_markup_parse_context_free(parser->hostContext);
	g_slist_free(parser->hostActions);
	g_markup_parse_context_free(parser->topologyContext);
	g_slist_free(parser->topologyActions);

	MAGIC_CLEAR(parser);
	g_free(parser);
}
