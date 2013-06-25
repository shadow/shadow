/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_LOAD_CDF_H_
#define SHD_LOAD_CDF_H_

#include "shadow.h"

typedef struct _LoadCDFAction LoadCDFAction;

struct _LoadCDFAction {
	Action super;
	GQuark id;
	GString* path;
	MAGIC_DECLARE;
};

LoadCDFAction* loadcdf_new(GString* name, GString* path);
void loadcdf_run(LoadCDFAction* action);
void loadcdf_free(LoadCDFAction* action);

#endif /* SHD_LOAD_CDF_H_ */
