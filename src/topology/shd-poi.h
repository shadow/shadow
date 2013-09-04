/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_POI_H_
#define SHD_POI_H_

#include "shadow.h"

typedef struct _PoI PoI;

PoI* poi_new();
void poi_free(PoI* poi);

#endif /* SHD_POI_H_ */
