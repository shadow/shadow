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
	GMarkupParser hostParser;
	GMarkupParseContext* hostContext;
	GSList* hostActions;

	GMarkupParser topologyParser;
	GMarkupParseContext* topologyContext;
	GSList* topologyActions;
	MAGIC_DECLARE;
};

Parser* parser_new();

/**
 * Parse the given filename and return a list of Actions that will produce the
 * specified topology (networks and links) when executed.
 */
GSList* parser_parseTopology(Parser* parser, GString* filename);

/**
 * Parse the given filename and return a list of Actions that will produce the
 * specified hosts (nodes and applications) when executed.
 */
GSList* parser_parseHosts(Parser* parser, GString* filename);

void parser_free(Parser* parser);

#endif /* SHD_PARSER_H_ */
