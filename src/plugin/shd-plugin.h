/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

struct _Plugin {
	GQuark id;
	GString* path;
	GModule* handle;
	ShadowPluginInitializeFunc init;
	PluginState* residentState;
	PluginState* defaultState;
	gboolean isRegisterred;
	/*
	 * TRUE from when we've called into plug-in code until the call completes.
	 * Note that the plug-in may get back into shadow code during execution, by
	 * calling one of the shadowlin functions or calling a function that we
	 * intercept. isShadowContext distinguishes this.
	 */
	gboolean isExecuting;
	/*
	 * Distinguishes which context we are in. Whenever the flow of execution
	 * passes into the plug-in, this is FALSE, and whenever it comes back to
	 * shadow, this is TRUE. This is used to determine if we should actually
	 * be intercepting functions or not, since we dont want to intercept them
	 * if they provide shadow with needed functionality.
	 *
	 * We must be careful to set this correctly at every boundry (shadowlib,
	 * interceptions, etc)
	 */
	gboolean isShadowContext;
	MAGIC_DECLARE;
};

Plugin* plugin_new(GQuark id, GString* filename);
void plugin_free(gpointer data);

void plugin_setShadowContext(Plugin* plugin, gboolean isShadowContext);

void plugin_registerResidentState(Plugin* plugin, PluginFunctionTable* callbackFunctions, guint nVariables, va_list variableArguments);
void plugin_executeNew(Plugin* plugin, PluginState* state, gint argcParam, gchar* argvParam[]);
void plugin_executeFree(Plugin* plugin, PluginState* state);

void plugin_executeReadable(Plugin* plugin, PluginState* state, gint socketParam);
void plugin_executeWritable(Plugin* plugin, PluginState* state, gint socketParam);
void plugin_executeWritableReadable(Plugin* plugin, PluginState* state, gint socketParam);
void plugin_executeReadableWritable(Plugin* plugin, PluginState* state, gint socketParam);

void plugin_executeGeneric(Plugin* plugin, PluginState* state, CallbackFunc callback, gpointer data, gpointer callbackArgument);

#endif /* SHD_PLUGIN_H_ */
