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

#ifndef SHD_PLUGIN_STATE_H_
#define SHD_PLUGIN_STATE_H_

#include "shadow.h"

typedef struct _PluginState PluginState;

struct _PluginState {
	PluginFunctionTable* functions;
	GSList* dataEntries;
	gsize nEntries;
	gsize totalEntrySize;
	MAGIC_DECLARE;
};

PluginState* pluginstate_new(PluginFunctionTable* callbackFunctions, guint nVariables, va_list vargs);
PluginState* pluginstate_copyNew(PluginState* state);
void pluginstate_copy(PluginState* sourceState, PluginState* destinationState);
void pluginstate_free(PluginState* state);

#endif /* SHD_PLUGIN_STATE_H_ */
