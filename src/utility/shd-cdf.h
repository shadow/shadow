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

/* Supported file format each line has "value cumulative_fraction". precision
 * on the value should be 3 decimal places, precision on the cumulative_fraction
 * should be 10 decimal places. these correspond to x and y values if graphing the cdf.
 */

#ifndef SHD_CDF_H_
#define SHD_CDF_H_

/* dont include shadow.h here - we use this in plugins and exes */
#include <glib.h>

typedef struct _CumulativeDistributionEntry CumulativeDistributionEntry;
struct _CumulativeDistributionEntry {
	gdouble fraction;
	gdouble value;
	MAGIC_DECLARE;
};

typedef struct _CumulativeDistribution CumulativeDistribution;
struct _CumulativeDistribution {
	GQuark id;
	GList* entries;
	MAGIC_DECLARE;
};


CumulativeDistribution* cdf_new(GQuark id, const gchar* filename);
CumulativeDistribution* cdf_generate(GQuark id, guint base_center, guint base_width, guint tail_width);
void cdf_free(gpointer data);

gdouble cdf_getValue(CumulativeDistribution* cdf, gdouble percentile);
gdouble cdf_getMinimumValue(CumulativeDistribution* cdf);
gdouble cdf_getMaximumValue(CumulativeDistribution* cdf);
gdouble cdf_getRandomValue(CumulativeDistribution* cdf);

#endif /* SHD_CDF_H_ */
