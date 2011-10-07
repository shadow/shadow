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
	ELEMENT_KILL,
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
	ATTRIBUTE_TIME,
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
	GQueue* actions;
	gboolean hasValidationError;
	MAGIC_DECLARE;
};

typedef struct _ParserValues ParserValues;

struct _ParserValues {
	/* represents a unique ID */
	GString* name;
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
	/* string of arguments that will be passed to the application */
	GString* arguments;
	/* holds the unique ID name of an application */
	GString* application;
	/* time in seconds */
	guint64 time;
	/* holds the unique ID name of a CDF for bandwidth (KiB/s) */
	GString* bandwidthup;
	/* holds the unique ID name of a CDF for bandwidth (KiB/s) */
	GString* bandwidthdown;
	/* holds the unique ID name of a CDF for CPU delay */
	GString* cpu;
	guint64 quantity;
	/* holds the unique ID name of a network */
	GString* network;
	/* holds the unique ID name of a network */
	GString* networka;
	/* holds the unique ID name of a network */
	GString* networkb;
	/* holds the unique ID name of a CDF for latency (milliseconds) */
	GString* latency;
	/* holds the unique ID name of a CDF for latency (milliseconds) */
	GString* latencyab;
	/* holds the unique ID name of a CDF for latency (milliseconds) */
	GString* latencyba;
	/* fraction between 0 and 1 - liklihood that a packet gets dropped */
	gdouble reliability;
	/* fraction between 0 and 1 - liklihood that a packet gets dropped */
	gdouble reliabilityab;
	/* fraction between 0 and 1 - liklihood that a packet gets dropped */
	gdouble reliabilityba;
	MAGIC_DECLARE;
};

Parser* parser_new();
void parser_free(Parser* parser);

/**
 * Parse the given filename and return a Queue of Actions that will produce the
 * specified topology (networks and links) and hosts (nodes and applications)
 * when executed. The caller owns the returned Queue and must properly free it.
 */
gboolean parser_parse(Parser* parser, GString* filename, GQueue* actions);

#endif /* SHD_PARSER_H_ */
