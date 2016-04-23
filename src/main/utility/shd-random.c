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
    gdouble randomFraction = random_nextDouble(random);
    gdouble maxUint = (gdouble)UINT_MAX;
    uint randomUint = (uint)(randomFraction * maxUint);
    return (gint)randomUint;
}

gdouble random_nextDouble(Random* random) {
    utility_assert(random);
    /* returns 0 to RAND_MAX, which is only 31 bits */
    gint randomValue = rand_r(&(random->seedState));
    return (gdouble)(((gdouble)randomValue) / ((gdouble)RAND_MAX));
}

void random_nextNBytes(Random* random, guchar* buffer, gint nbytes) {
    utility_assert(random);
    gint offset = 0;
    while(offset < nbytes) {
        gint randInt = random_nextInt(random);
        gint n = MIN((nbytes - offset), sizeof(gint));
        memmove(&buffer[offset], &randInt, n);
        offset += n;
    }
}
