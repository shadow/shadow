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

#include <glib.h>
#include <stdlib.h>
#include "global.h"
#include "vector.h"

vector_tp vector_create (void) {
	vector_tp rv = malloc (sizeof(*rv));

	if(!rv)
		printfault(EXIT_NOMEM, "vector_create: Out of memory");

	rv->num_elems = 0;
	rv->elems = NULL;

	return rv;
}

size_t vector_size (vector_tp v) {
	return v->num_elems;
}

gpointer vector_get (vector_tp v, guint i) {
	if(i >= v->num_elems)
		return NULL;
	return v->elems[i];
}

gpointer vector_remove (vector_tp v, guint i) {
	gpointer rv;

	if(i >= v->num_elems)
		return NULL;

	rv = v->elems[i];
	v->elems[i] = v->elems[v->num_elems--];

	if(v->num_elems == 0) {
		free(v->elems);
		v->num_allocated = 0;
		v->elems = NULL;
	} else if(v->num_elems < v->num_allocated/2 && v->num_allocated > VECTOR_MIN_SIZE) {
		v->num_allocated /= 2;
		v->elems = realloc(v->elems, v->num_allocated * sizeof(*v->elems));
	}

	return rv;
}

void vector_push (vector_tp v, gpointer o) {
	if(v->num_elems == v->num_allocated) {
		if(v->num_allocated == 0) {
			v->num_allocated = VECTOR_MIN_SIZE;
			v->elems = malloc(v->num_allocated * sizeof(*v->elems));
		} else {
			v->num_allocated *= 2;
			v->elems = realloc(v->elems, v->num_allocated * sizeof(*v->elems));
		}

		if(!v->elems)
			printfault(EXIT_NOMEM, "vector_push: Out of memory");
	}

	v->elems[v->num_elems++] = o;
	return;
}

gpointer vector_pop (vector_tp v) {
	return vector_remove(v, v->num_elems - 1);
}

void vector_destroy (vector_tp v) {
	if(v->elems)
		free(v->elems);
	free(v);
}
