/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_HEARTBEAT_H_
#define SHD_HEARTBEAT_H_

#include "shadow.h"

typedef struct _HeartbeatEvent HeartbeatEvent;

HeartbeatEvent* heartbeat_new(Tracker* tracker);
void heartbeat_run(HeartbeatEvent* event, Host* node);
void heartbeat_free(HeartbeatEvent* event);

#endif /* SHD_HEARTBEAT_H_ */
