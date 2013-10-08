/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

void action_init(Action* a, RunnableFunctionTable* vtable) {
	utility_assert(a && vtable);
	MAGIC_INIT(a);
	MAGIC_INIT(vtable);
	a->priority = 0;
	runnable_init(&(a->super), vtable);
}

void action_setPriority(Action* a, gint priority) {
	MAGIC_ASSERT(a);
	a->priority = priority;
}

gint action_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Action* aa = a;
	const Action* ab = b;
	MAGIC_ASSERT(aa);
	MAGIC_ASSERT(ab);
	return aa->priority > ab->priority ? +1 : aa->priority == ab->priority ? 0 : -1;
}
