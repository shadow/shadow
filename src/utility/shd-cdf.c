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

#include "shadow.h"

#include <stdio.h>

static CumulativeDistributionEntry* cdfentry_create() {
	CumulativeDistributionEntry* entry = g_new0(CumulativeDistributionEntry, 1);
	MAGIC_INIT(entry);
	return entry;
}

static gint cdfentry_compare(gconstpointer a, gconstpointer b) {
	const CumulativeDistributionEntry* entryA = a;
	const CumulativeDistributionEntry* entryB = b;
	MAGIC_ASSERT(entryA);
	MAGIC_ASSERT(entryB);
	return entryA->value > entryB->value ? +1 : entryA->value == entryB->value ? 0 : -1;
}

static void cdfentry_free(gpointer data) {
	CumulativeDistributionEntry* entry = data;
	MAGIC_ASSERT(entry);
	MAGIC_CLEAR(entry);
	g_free(entry);
}

static GList* cdf_parse(const gchar* filename) {
	if(filename == NULL) {
		return NULL;
	}

	FILE* f = fopen(filename, "r");
	if(f == NULL) {
		perror("fopen()");
		return NULL;
	}

	/* start with an empty list of CDF entries */
	GList* entries = NULL;

	gint result = 0;
	while(!feof(f) && !ferror(f)) {
		CumulativeDistributionEntry* entry = cdfentry_create();
		result = fscanf(f, "%lf %lf\n", &(entry->value), &(entry->fraction));
		if(result != 2) {
			cdfentry_free(entry);
			g_list_free(entries);
			break;
		} else {
			entries = g_list_insert_sorted(entries, entry, cdfentry_compare);
		}
	}

	fclose(f);

	return entries;
}

CumulativeDistribution* cdf_new(GQuark id, const gchar* filename) {
	/* TODO We should use a binary tree for more efficient lookups */
	GList* ol = cdf_parse(filename);
	if(ol != NULL) {
		CumulativeDistribution* cdf = g_new0(CumulativeDistribution, 1);
		MAGIC_INIT(cdf);
		cdf->entries = ol;
		cdf->id = id;
		return cdf;
	} else {
		return NULL;
	}
}

/**
 * Provides the underlying model for the network layer
 *
 * Based on the delay measurements in turbo-King
 * http://inl.info.ucl.ac.be/blogs/08-04-23-turbo-king-framework-large-scale-ginternet-delay-measurements
 * Paper:  http://irl.cs.tamu.edu/people/derek/papers/infocom2008.pdf
 * Note that we are looking mostly at link delay since we are modeling an inter AS delay
 *
 * We expect a CDF as follows:

   1|                         +++++++++++++++
    |                     +++
    |                  ++
    |                 +
    |                +
    |                +
    |                +
    |                +
    |                +
    |                +
    |               +
    |               +
    |              +
   0+++++++++++++++-----------------------------
    0                |
                Base Delay
                 |<----->|<----------|
                  Width      Tail
 */

// TODO check if we should do this in cdf_generate
//static guint vci_model_delay(vci_netmodel_tp netmodel) {
//	if(netmodel != NULL) {
//		float flBase = 1.0f;
//		float flRandWidth = 0.0f;
//		gint i = 0;
//
//		for(i=0;i<=VCI_NETMODEL_TIGHTNESS_FACTOR ;i++)
//		{
//			// Cummulatively computes random delay values
//			flBase = flBase * (1.0f - (dvn_rand_fast(RAND_MAX) / ((float)RAND_MAX/2)));
//		}
//
//		if(flBase < 0)
//			flRandWidth = flBase * netmodel->width;// Scales it to the desired width
//		else
//			flRandWidth = flBase * netmodel->tail_width;// Models the long tail
//
//		return (guint) netmodel->base_delay + (guint)flRandWidth;
//	} else {
//		return 0;
//	}
//}

CumulativeDistribution* cdf_generate(GQuark id, guint base_center, guint base_width, guint tail_width) {
	CumulativeDistribution* cdf = g_new0(CumulativeDistribution, 1);
	MAGIC_INIT(cdf);
	cdf->entries = NULL;
	cdf->id = id;

	/* TODO fix this - use model from vci?? */
	CumulativeDistributionEntry* entry1 = cdfentry_create();
	CumulativeDistributionEntry* entry2 = cdfentry_create();
	CumulativeDistributionEntry* entry3 = cdfentry_create();
	CumulativeDistributionEntry* entry4 = cdfentry_create();

	entry1->fraction = 0.10;
	entry1->value = (gdouble) (base_center - base_width);
	entry2->fraction = 0.80;
	entry2->value = (gdouble) (base_center);
	entry3->fraction = 0.90;
	entry3->value = (gdouble) (base_center + base_width);
	entry4->fraction = 0.95;
	entry4->value = (gdouble) (base_center + base_width + tail_width);

	cdf->entries = g_list_insert_sorted(cdf->entries, entry1, cdfentry_compare);
	cdf->entries = g_list_insert_sorted(cdf->entries, entry2, cdfentry_compare);
	cdf->entries = g_list_insert_sorted(cdf->entries, entry3, cdfentry_compare);
	cdf->entries = g_list_insert_sorted(cdf->entries, entry4, cdfentry_compare);

	return cdf;
}

void cdf_free(gpointer data) {
	CumulativeDistribution* cdf = data;
	MAGIC_ASSERT(cdf);
	g_list_free_full(cdf->entries, cdfentry_free);
	MAGIC_CLEAR(cdf);
	g_free(cdf);
}

gdouble cdf_getValue(CumulativeDistribution* cdf, gdouble percentile) {
	MAGIC_ASSERT(cdf);
	g_assert(percentile >= 0.0 && percentile <= 1.0);

	GList* item;
	if(percentile == 1.0) {
		item = g_list_last(cdf->entries);
	} else if(percentile == 0.0) {
		item = g_list_first(cdf->entries);
	} else {
		guint position = (guint) (percentile * (g_list_length(cdf->entries)));
		item = g_list_nth(cdf->entries, position);
	}

	if(item) {
		CumulativeDistributionEntry* entry = item->data;
		MAGIC_ASSERT(entry);
		return entry->value;
	}

	return (gdouble) 0;
}

gdouble cdf_getRandomValue(CumulativeDistribution* cdf) {
	MAGIC_ASSERT(cdf);
	gdouble percentile = dvn_rand_unit();
	return cdf_getValue(cdf, percentile);
}
