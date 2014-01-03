/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_AIMD_H_
#define SHD_TCP_AIMD_H_

#include "shadow.h"

typedef struct _AIMD AIMD;

AIMD* aimd_new(gint cwnd, gint ssthresh);

#endif /* SHD_AIMD_H_ */
