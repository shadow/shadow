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
