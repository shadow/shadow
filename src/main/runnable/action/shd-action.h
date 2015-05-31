/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ACTION_H_
#define SHD_ACTION_H_

#include "shadow.h"

typedef struct _Action Action;

void action_init(Action* a, RunnableFunctionTable* vtable);
void action_setPriority(Action* a, gint priority);
gint action_compare(gconstpointer a, gconstpointer b, gpointer user_data);

#endif /* SHD_ACTION_H_ */
