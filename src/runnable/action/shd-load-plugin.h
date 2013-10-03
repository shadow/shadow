/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_LOAD_PLUGIN_H_
#define SHD_LOAD_PLUGIN_H_

#include "shadow.h"

typedef struct _LoadPluginAction LoadPluginAction;

LoadPluginAction* loadplugin_new(GString* name, GString* path);
void loadplugin_run(LoadPluginAction* action);
void loadplugin_free(LoadPluginAction* action);

#endif /* SHD_LOAD_PLUGIN_H_ */
