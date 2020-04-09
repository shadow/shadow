/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_RANDOM_H_
#define SHD_RANDOM_H_

#include <glib.h>

/**
 * An opaque structure representing a random source.
 */
typedef struct _Random Random;

/**
 * Create a new thread-safe random source using seed as the initial state.
 * @param seed
 * @return a pointer to the new random source
 */
Random* random_new(guint seed);

/**
 * Frees the memory allocated for the random source.
 * @param random the random source
 */
void random_free(Random* random);

/**
 * Gets the next integer in the range [0, RAND_MAX] from the random source.
 * @param random the random source
 * @return the next integer in the range [0, RAND_MAX]
 */
gint random_rand(Random* random);

/**
 * Gets the next double in the range [0,1] from the random source.
 * @param random the random source
 * @return the next double in the range [0,1]
 */
gdouble random_nextDouble(Random* random);

guint random_nextUInt(Random* random);

/**
 * Gets the next nbytes in the range [0, RAND_MAX] from the random source.
 * @param random the random source
 * @param buffer the buffer to copy the random bytes to
 * @param nbytes number of bytes to copy to the buffer
 */
void random_nextNBytes(Random* random, guchar* buffer, gsize nbytes);

#endif /* SHD_RANDOM_H_ */
