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

#ifndef SHD_PARSER_H_
#define SHD_PARSER_H_

#include "shadow.h"

/**
 * @addtogroup Parser
 * @{
 * Use this module to parse XML input files.
 */

/**
 * An opaque object used to store state while parsing an XML file for
 * Shadow simulation input. The member of this struct are private and should not
 * be accessed directly.
 */
typedef struct _Parser Parser;

/**
 * Create a new parser. The parser is capable of parsing Shadow XML simulation
 * files.
 *
 * @return a pointer to a newly allocated #Parser. The pointer should be freed
 * with parser_free().
 */
Parser* parser_new();

/**
 * Frees a previously allocated parser.
 *
 * @param parser a pointer to a #Parser allocated with parser_new()
 */
void parser_free(Parser* parser);

/**
 * Convenience function that reads the contents from a file and parses it
 * by calling parser_parseContents().
 *
 * @param parser a pointer to a #Parser allocated with parser_new()
 * @param filename a #GString holding the path to an XML file formated for
 * Shadow input.
 * @param actions a pointer to an existing #GQueue
 *
 * @return TRUE if filename was successfully parsed and validated, FALSE otherwise
 */
gboolean parser_parseFile(Parser* parser, GString* filename, GQueue* actions);

/**
 * Parse the given contents and add #Action objects to the given actions queue.
 * Execution of the actions will produce the topology specified in the XML
 * contents (networks and links) and hosts (nodes and software)
 * when executed. The caller owns the #GQueue before and after calling this
 * function.
 *
 * @param parser a pointer to a #Parser allocated with parser_new()
 * @param contents the XML contents to parse
 * @param length the length of the XML contents string
 * @param actions a pointer to an existing #GQueue
 * @return TRUE if contents were successfully parsed and validated, FALSE otherwise
 */
gboolean parser_parseContents(Parser* parser, gchar* contents, gsize length, GQueue* actions);

/** @} */

#endif /* SHD_PARSER_H_ */
