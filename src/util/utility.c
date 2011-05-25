/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>

#include "global.h"
#include "utility.h"

char * file_get_contents(const char * filename) {
	char * contents;
	unsigned int length;
	FILE * file;

	if(!filename)
		return NULL;

	file=fopen(filename, "r");
	if(!file)
		return NULL;

	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);

	if(length == 0)
		contents = NULL;
	else {
		contents = malloc(length+1);
		if(!contents)
			printfault(EXIT_NOMEM, "file_get_contents: Out of memory");
		fread(contents, 1, length, file);
	}
	fclose(file);

	return contents;
}

/**
 * writes the given message to stderr and
 * and exits with the given exit code.
 *
 */
void printfault(int error, char *fmt, ...) {
	va_list arg;
	va_start(arg, fmt);

	vfprintf(stderr, fmt, arg);

	va_end(arg);
	exit(error);
}

gint *int_key(int key) {
    gint *ret = g_malloc(sizeof(gint));
    *ret = key;
    return ret;
}

