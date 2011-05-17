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

/* Supported file format each line has "value cumulative_fraction". precision
 * on the value should be 3 decimal places, precision on the cumulative_fraction
 * should be 10 decimal places. these correspond to x and y values if graphing the cdf.
 */

#ifndef SHD_CDF_H_
#define SHD_CDF_H_

#include <stdint.h>
#include <stddef.h>

#include "orderedlist.h"

#define DOUBLE_2_UINT64(x) ((uint64_t)((x)*10000000000))
#define UINT64_2_DOUBLE(x) ((double)((x)/10000000000))

typedef struct cdf_s {
	orderedlist_tp ol;
} cdf_t, *cdf_tp;

cdf_tp cdf_create(const char* filename);
cdf_tp cdf_generate(unsigned int base_center, unsigned int base_width, unsigned int tail_width);
void cdf_destroy(cdf_tp cdf);

double cdf_min_value(cdf_tp cdf);
double cdf_max_value(cdf_tp cdf);
double cdf_random_value(cdf_tp cdf);

#endif /* SHD_CDF_H_ */
