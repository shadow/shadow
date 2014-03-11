/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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

/**
 * An opaque structure representing a Cumulative Distribution.
 */
typedef struct _CumulativeDistribution CumulativeDistribution;

/**
 * Create a new CumulativeDistribution with data from the given filename. The
 * file is parsed for lines of the form "value fraction". Each such entry will
 * be sorted internally.
 *
 * @param id
 * @param filename
 */
CumulativeDistribution* cdf_new(GQuark id, const gchar* filename);

/**
 *
 */
CumulativeDistribution* cdf_generate(GQuark id, guint base_center, guint base_width, guint tail_width);

CumulativeDistribution* cdf_newFromQueue(GQueue* doubleValues);

/**
 *
 */
void cdf_free(gpointer data);


gdouble cdf_getValue(CumulativeDistribution* cdf, gdouble percentile);
gdouble cdf_getMinimumValue(CumulativeDistribution* cdf);
gdouble cdf_getMaximumValue(CumulativeDistribution* cdf);

GQuark* cdf_getIDReference(CumulativeDistribution* cdf);

#endif /* SHD_CDF_H_ */
