/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
