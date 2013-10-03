/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_GENERATE_CDF_H_
#define SHD_GENERATE_CDF_H_

#include "shadow.h"

typedef struct _GenerateCDFAction GenerateCDFAction;

GenerateCDFAction* generatecdf_new(GString* name, guint64 center, guint64 width,
		guint64 tail);
void generatecdf_run(GenerateCDFAction* action);
void generatecdf_free(GenerateCDFAction* action);

#endif /* SHD_GENERATE_CDF_H_ */
