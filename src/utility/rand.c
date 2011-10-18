/*
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
#include "rand.h"

gdouble dvn_rand_unit() {
	return (gdouble)rand() / (gdouble)RAND_MAX;
}

guint dvn_rand_fast(guint max) {
	return rand() % max; /* note: i'm WELL AWARE this isn't an even distribution. */
}

guint dvn_rand(guint max) {
	return dvn_rand_unit() * max;
}

void dvn_rand_seed(guint seed) {
	srand(seed);
}

gint *gint_key(gint key) {
    gint *ret = g_malloc0(sizeof(gint));
    *ret = key;
    return ret;
}

gint gint_compare_func(gpointer a, gpointer b) {
    gint x = GPOINTER_TO_INT(a);
    gint y = GPOINTER_TO_INT(b);
    if(x < y) return -1;
    if(x > y) return 1;
    return 0;
}

gboolean g_int16_equal(gconstpointer v1, gconstpointer v2) {
	/* make sure upper bits are zero */
	gint k1 = 0, k2 = 0;
	k1 = (gint) *((gint16*)v1);
	k2 = (gint) *((gint16*)v2);
	return g_int_equal(&k1, &k2);
}

guint g_int16_hash(gconstpointer v1) {
	/* make sure upper bits are zero */
	gint k = 0;
	k = (gint) *((gint16*)v1);
	return g_int_hash(&k);
}
