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

#include <glib.h>
#include <stdlib.h>

#include "shd-random.h"

struct _Random {
	guint seedState;
	guint initialSeed;
};

Random* random_new(guint seed) {
	Random* random = g_new0(Random, 1);
	random->initialSeed = seed;
	random->seedState = seed;
	return random;
}

void random_free(Random* random) {
	g_assert(random);
	g_free(random);
}

gint random_nextRandom(Random* random) {
	return (gint) rand_r(&(random->seedState));
}

gdouble random_nextDouble(Random* random) {
	g_assert(random);
	return (gdouble)(((gdouble)rand_r(&(random->seedState))) / ((gdouble)RAND_MAX));
}
