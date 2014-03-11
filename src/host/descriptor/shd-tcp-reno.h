/*
 * The Shadow Simulator
 * Copyright (c) 2013-2014, John Geddes
 * See LICENSE for licensing information
 */

#ifndef SHD_TCP_RENO_H_
#define SHD_TCP_RENO_H_

#include "shadow.h"

typedef struct _Reno Reno;

Reno* reno_new(gint cwnd, gint ssthresh);

#endif /* SHD_RENO_H_ */
