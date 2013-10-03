/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_LISTENER_H_
#define SHD_LISTENER_H_

#include "shadow.h"

typedef void (*CallbackFunc)(gpointer data, gpointer callbackArgument);

typedef struct _Listener Listener;

Listener* listener_new(CallbackFunc callback, gpointer data, gpointer callbackArgument);
void listener_free(gpointer data);
void listener_notify(gpointer data);

#endif /* SHD_LISTENER_H_ */
