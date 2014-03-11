/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_AIMD_H_
#define SHD_TCP_AIMD_H_

#include "shadow.h"

typedef struct _AIMD AIMD;

AIMD* aimd_new(gint cwnd, gint ssthresh);

#endif /* SHD_AIMD_H_ */
