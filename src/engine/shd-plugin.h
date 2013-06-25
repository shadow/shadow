/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PLUGIN_H_
#define SHD_PLUGIN_H_

#include "shadow.h"

typedef struct _Plugin Plugin;
typedef gpointer PluginState;

Plugin* plugin_new(GQuark id, GString* filename);
void plugin_free(gpointer data);

PluginState plugin_newDefaultState(Plugin* plugin);
void plugin_freeState(Plugin* plugin, PluginState state);

void plugin_setShadowContext(Plugin* plugin, gboolean isShadowContext);
gboolean plugin_isShadowContext(Plugin* plugin);
GQuark* plugin_getID(Plugin* plugin);

void plugin_registerResidentState(Plugin* plugin, PluginNewInstanceFunc new, PluginNotifyFunc free, PluginNotifyFunc notify);
void plugin_executeNew(Plugin* plugin, PluginState state, gint argcParam, gchar* argvParam[]);
void plugin_executeFree(Plugin* plugin, PluginState state);
void plugin_executeNotify(Plugin* plugin, PluginState state);
void plugin_executeGeneric(Plugin* plugin, PluginState state, CallbackFunc callback, gpointer data, gpointer callbackArgument);

#endif /* SHD_PLUGIN_H_ */
