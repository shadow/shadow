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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "shd-cdf.h"

static orderedlist_tp cdf_parse(const char* filename) {
	if(filename == NULL) {
		return NULL;
	}

	FILE* f = fopen(filename, "r");
	if(f == NULL) {
		perror("fopen()");
		return NULL;
	}

	orderedlist_tp ol = orderedlist_create();

	int result = 0;
	double key = 0;
	while(!feof(f) && !ferror(f)) {
		double* value = calloc(1, sizeof(double));
		result = fscanf(f, "%lf %lf\n", value, &key);
		if(result != 2) {
			orderedlist_destroy(ol, 1);
			ol = NULL;
			free(value);
		} else {
			uint64_t list_key = DOUBLE_2_UINT64(key);
			orderedlist_add(ol, list_key, value);
		}
	}

	fclose(f);

	return ol;
}

cdf_tp cdf_create(const char* filename) {
	orderedlist_tp ol = cdf_parse(filename);
	if(ol != NULL) {
		cdf_tp cdf = malloc(sizeof(cdf_t));
		cdf->ol = ol;
		return cdf;
	} else {
		return NULL;
	}
}

cdf_tp cdf_generate(unsigned int base_center, unsigned int base_width, unsigned int tail_width) {
	cdf_tp cdf = malloc(sizeof(cdf_t));
	cdf->ol = orderedlist_create();

	/* TODO fix this - use model from vci?? */
	double* value1 = calloc(1, sizeof(double));
	double* value2 = calloc(1, sizeof(double));
	double* value3 = calloc(1, sizeof(double));
	double* value4 = calloc(1, sizeof(double));

	*value1 = base_center - base_width;
	*value2 = base_center;
	*value3 = base_center + base_width;
	*value4 = base_center + base_width + tail_width;

	orderedlist_add(cdf->ol, DOUBLE_2_UINT64(0.10), value1);
	orderedlist_add(cdf->ol, DOUBLE_2_UINT64(0.80), value2);
	orderedlist_add(cdf->ol, DOUBLE_2_UINT64(0.90), value3);
	orderedlist_add(cdf->ol, DOUBLE_2_UINT64(0.95), value4);

	return cdf;
}

void cdf_destroy(cdf_tp cdf) {
	if(cdf != NULL) {
		orderedlist_destroy(cdf->ol, 1);
		free(cdf);
	}
}

double cdf_min_value(cdf_tp cdf) {
	if(cdf != NULL) {
		double* value = orderedlist_peek_first_value(cdf->ol);
		if(value != NULL) {
			double d = *value;
			return d;
		}
	}
	return 0;
}

double cdf_max_value(cdf_tp cdf) {
	if(cdf != NULL) {
		double* value = orderedlist_peek_last_value(cdf->ol);
		if(value != NULL) {
			double d = *value;
			return d;
		}
	}
	return 0;
}

double cdf_random_value(cdf_tp cdf) {
	if(cdf != NULL) {
		uint64_t list_key = DOUBLE_2_UINT64((double)rand() / (double)RAND_MAX);
		double* value = orderedlist_ceiling_value(cdf->ol, list_key);
		if(value != NULL) {
			double ceiling = *value;
			return ceiling;
		}
	}
	return 0;
}
