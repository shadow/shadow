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

#ifndef SHD_PARSER_H_
#define SHD_PARSER_H_

#include "shadow.h"

/* NOTE - they MUST be synced with ParserElementStrings in shd-parser.c */
typedef enum {
	ELEMENT_PLUGIN,
	ELEMENT_CDF,
	ELEMENT_APPLICATION,
	ELEMENT_NODE,
	ELEMENT_NETWORK,
	ELEMENT_LINK,
} ParserElements;

/* NOTE - they MUST be synced with ParserAttributeStrings in shd-parser.c */
typedef enum {
	ATTRIBUTE_NAME,
	ATTRIBUTE_PATH,
	ATTRIBUTE_CENTER,
	ATTRIBUTE_WIDTH,
	ATTRIBUTE_TAIL,
	ATTRIBUTE_PLUGIN,
	ATTRIBUTE_ARGUMENTS,
	ATTRIBUTE_APPLICATION,
	ATTRIBUTE_BANDWIDTHUP,
	ATTRIBUTE_BANDWIDTHDOWN,
	ATTRIBUTE_CPU,
	ATTRIBUTE_QUANTITY,
	ATTRIBUTE_NETWORK,
	ATTRIBUTE_NETWORKA,
	ATTRIBUTE_NETWORKB,
	ATTRIBUTE_LATENCY,
	ATTRIBUTE_LATENCYAB,
	ATTRIBUTE_LATENCYBA,
	ATTRIBUTE_RELIABILITY,
	ATTRIBUTE_RELIABILITYAB,
	ATTRIBUTE_RELIABILITYBA,
} ParserAttributes;

typedef struct _Parser Parser;

struct _Parser {
	GMarkupParser parser;
	GMarkupParseContext* context;
	GSList* actions;
	MAGIC_DECLARE;
};

typedef struct _ParserValues ParserValues;

struct _ParserValues {
	GString* name;
	GString* path;
	guint64 center;
	guint64 width;
	guint64 tail;
	GString* plugin;
	GString* arguments;
	GString* application;
	guint64 bandwidthup;
	guint64 bandwidthdown;
	GString* cpu;
	guint64 quantity;
	GString* network;
	GString* networka;
	GString* networkb;
	GString* latency;
	GString* latencyab;
	GString* latencyba;
	gdouble reliability;
	gdouble reliabilityab;
	gdouble reliabilityba;
	MAGIC_DECLARE;
};

Parser* parser_new();
void parser_free(Parser* parser);

/**
 * Parse the given filename and return a list of Actions that will produce the
 * specified topology (networks and links) and hosts (nodes and applications)
 * when executed.
 */
GSList* parser_parse(Parser* parser, GString* filename);


#endif /* SHD_PARSER_H_ */
