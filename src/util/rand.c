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

#include <stdlib.h>
#include "rand.h"

double dvn_rand_unit() {
	return (double)rand() / (double)RAND_MAX;
}

unsigned int dvn_rand_fast(unsigned int max) {
	return rand() % max; /* note: i'm WELL AWARE this isn't an even distribution. */
}

unsigned int dvn_rand(unsigned int max) {
	return dvn_rand_unit() * max;
}

void dvn_rand_seed(unsigned int seed) {
	srand(seed);
}
