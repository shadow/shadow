/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SHD_PLUGIN_H_
#define SHD_PLUGIN_H_

#include "shadow.h"

typedef struct _Plugin Plugin;

Plugin* plugin_new(GQuark id, GString* filename);
void plugin_free(gpointer data);
PluginState* plugin_newDefaultState(Plugin* plugin);

void plugin_setShadowContext(Plugin* plugin, gboolean isShadowContext);
gboolean plugin_isShadowContext(Plugin* plugin);
GQuark* plugin_getID(Plugin* plugin);

void plugin_registerResidentState(Plugin* plugin, PluginFunctionTable* callbackFunctions, guint nVariables, va_list variableArguments);
void plugin_executeNew(Plugin* plugin, PluginState* state, gint argcParam, gchar* argvParam[]);
void plugin_executeFree(Plugin* plugin, PluginState* state);
void plugin_executeNotify(Plugin* plugin, PluginState* state);
void plugin_executeGeneric(Plugin* plugin, PluginState* state, CallbackFunc callback, gpointer data, gpointer callbackArgument);

#endif /* SHD_PLUGIN_H_ */
