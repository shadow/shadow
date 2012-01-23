/**
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
