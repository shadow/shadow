/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_REGISTRY_H_
#define SHD_REGISTRY_H_

#include "shadow.h"

typedef struct _Registry Registry;

struct _Registry {
	GHashTable* storage;

	MAGIC_DECLARE;
};

Registry* registry_new();
void registry_free(Registry* registry);

void registry_register(Registry* registry, gint index,
		GDestroyNotify keyDestroyFunc, GDestroyNotify valueDestroyFunc);
void registry_put(Registry* registry, gint index, GQuark* key, gpointer value);
gpointer registry_get(Registry* registry, gint index, GQuark* key);
GList* registry_getAll(Registry* registry, gint index);

#endif /* SHD_REGISTRY_H_ */
