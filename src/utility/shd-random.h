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

#ifndef SHD_RANDOM_H_
#define SHD_RANDOM_H_

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
gint random_nextInt(Random* random);

/**
 * Gets the next double in the range [0,1] from the random source.
 * @param random the random source
 * @return the next double in the range [0,1]
 */
gdouble random_nextDouble(Random* random);

#endif /* SHD_RANDOM_H_ */
