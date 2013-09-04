/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_STOP_APPLICATION_H_
#define SHD_STOP_APPLICATION_H_

#include "shadow.h"

/**
 * Stop a given application for a given Node.
 */

typedef struct _StopApplicationEvent StopApplicationEvent;

StopApplicationEvent* stopapplication_new(Application* application);
void stopapplication_run(StopApplicationEvent* event, Host* node);
void stopapplication_free(StopApplicationEvent* event);

#endif /* SHD_STOP_APPLICATION_H_ */
