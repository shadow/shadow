/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <stdlib.h>

#include "shd-utility.h"
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
	utility_assert(random);
	g_free(random);
}

gint random_nextInt(Random* random) {
	return (gint) rand_r(&(random->seedState));
}

gdouble random_nextDouble(Random* random) {
	utility_assert(random);
	return (gdouble)(((gdouble)rand_r(&(random->seedState))) / ((gdouble)RAND_MAX));
}

void random_nextNBytes(Random* random, guchar* buffer, gint nbytes) {
    utility_assert(random);

    gint randInt;
    for(gint i = 0; i < nbytes; i++) {
        /* each random integer gives us 4 bytes, so only generate a new
         * one for every 4 bytes we copy over */
        gint offset = i % 4;
        if(offset == 0) {
            randInt = random_nextInt(random);
        }

        /* get the correct byte from the random integer by bit shifting */
        buffer[i] = (randInt >> (offset * 8)) & 0xFF;
    }
}
