/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_START_APPLICATION_H_
#define SHD_START_APPLICATION_H_

#include "shadow.h"

/**
 * Start an application for a given Node. This event is basically a bootstrap
 * event for each node.
 */

typedef struct _StartApplicationEvent StartApplicationEvent;

StartApplicationEvent* startapplication_new(Application* application);
void startapplication_run(StartApplicationEvent* event, Node* node);
void startapplication_free(StartApplicationEvent* event);

#endif /* SHD_START_APPLICATION_H_ */
