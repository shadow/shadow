/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CALLBACK_H_
#define SHD_CALLBACK_H_

#include "shadow.h"

typedef struct _CallbackEvent CallbackEvent;

CallbackEvent* callback_new(CallbackFunc callback, gpointer data, gpointer callbackArgument);
void callback_run(CallbackEvent* event, Node* node);
void callback_free(CallbackEvent* event);

#endif /* SHD_CALLBACK_H_ */
