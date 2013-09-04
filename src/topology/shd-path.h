/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PATH_H_
#define SHD_PATH_H_

#include "shadow.h"

typedef struct _Path Path;

Path* path_new();
void path_free(Path* path);

#endif /* SHD_PATH_H_ */
