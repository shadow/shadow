/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_CUBIC_H_
#define SHD_TCP_CUBIC_H_

#include "shadow.h"

typedef struct _Cubic Cubic;

Cubic* cubic_new(gint cwnd, gint ssthresh);

#endif /* SHD_CUBIC_H_ */
