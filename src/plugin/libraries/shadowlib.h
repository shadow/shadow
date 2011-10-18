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

#ifndef SHADOWLIB_H_
#define SHADOWLIB_H_

#include <netinet/in.h>

/* function signatures for what each plugin must implement */
typedef void (*PluginNewInstanceFunc)(gint, gchar *[]);
typedef void (*PluginFreeInstanceFunc)();
typedef void (*PluginSocketReadableFunc)(gint);
typedef void (*PluginSocketWritableFunc)(gint);

typedef struct _PluginFunctionTable PluginFunctionTable;

struct _PluginFunctionTable {
	PluginNewInstanceFunc new;
	PluginFreeInstanceFunc free;
	PluginSocketReadableFunc readable;
	PluginSocketWritableFunc writable;
};

/*
 * signature for plug-in callback functions
 */
typedef void (*ShadowPluginCallbackFunc)(gpointer data);

/* function signatures for available shadow functions */
typedef gboolean (*ShadowlibRegisterFunc)(PluginFunctionTable* callbackFunctions, guint nVariables, ...);
typedef void (*ShadowlibLogFunc)(GLogLevelFlags level, const gchar* functionName, gchar* format, ...);
typedef in_addr_t (*ShadowlibResolveHostnameFunc)(gchar* name);
typedef gboolean (*ShadowlibResolveIPAddressFunc)(in_addr_t addr, gchar* name_out, gint name_out_len);
typedef in_addr_t (*ShadowlibGetIPAddressFunc)();
typedef gboolean (*ShadowlibGetHostnameFunc)(gchar* name_out, gint name_out_len);
typedef void (*ShadowlibCreateCallbackFunc)(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay);

typedef struct _ShadowlibFunctionTable ShadowlibFunctionTable;

struct _ShadowlibFunctionTable {
	ShadowlibRegisterFunc registration;
	ShadowlibLogFunc log;
	ShadowlibResolveHostnameFunc resolveHostname;
	ShadowlibResolveIPAddressFunc resolveIP;
	ShadowlibGetHostnameFunc getHostname;
	ShadowlibGetIPAddressFunc getIP;
	ShadowlibCreateCallbackFunc createCallback;
};

/* Plug-ins must implement this function to communicate with Shadow.
 * the function name symbol must be "__shadow_plugin_init__" */
typedef void (*ShadowPluginInitializeFunc)(ShadowlibFunctionTable* shadowlibFunctions);

#endif /* SHADOWLIB_H_ */
