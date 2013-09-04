/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_POP_H_
#define SHD_POP_H_

#include "shadow.h"

typedef struct _PoP PoP;

PoP* pop_new();
void pop_free(PoP* pop);

#endif /* SHD_POP_H_ */
