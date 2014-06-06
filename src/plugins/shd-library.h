/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_LIBRARY_H_
#define SHD_LIBRARY_H_

#include <netinet/in.h>

/**
 * plug-ins must implement a function with this name to hook into Shadow. We
 * call this function during plugin initialization. A symbol with this name
 * must exist or the dlsym lookup will fail.
 */
#define PLUGININITSYMBOL "__shadow_plugin_init__"

/**
 * Global symbols added after using LLVM to automatically extract variable state
 */
#define PLUGINGLOBALSSYMBOL "__hoisted_globals"
#define PLUGINGLOBALSSIZESYMBOL "__hoisted_globals_size"
#define PLUGINGLOBALSPOINTERSYMBOL "__hoisted_globals_pointer"

/* Shadow log levels, mirrors glib. */
typedef enum {
  SHADOW_LOG_LEVEL_ERROR             = 1 << 2,       /* always fatal */
  SHADOW_LOG_LEVEL_CRITICAL          = 1 << 3,
  SHADOW_LOG_LEVEL_WARNING           = 1 << 4,
  SHADOW_LOG_LEVEL_MESSAGE           = 1 << 5,
  SHADOW_LOG_LEVEL_INFO              = 1 << 6,
  SHADOW_LOG_LEVEL_DEBUG             = 1 << 7,
} ShadowLogLevel;

/**
 * Signature of a function that Shadow calls when creating a new node instance
 * with the plug-in. The parameters are meant to mirror those passed to the
 * main() function of a standard C program.
 *
 * @param argc the number of arguments passed in the argument vector
 * @param argv a vector of arguments for node creation, as parsed from the XML
 * simulation input file
 *
 * @see #PluginFunctionTable
 */
typedef void (*PluginNewInstanceFunc)(int argc, char* argv[]);

/**
 * Signature of a function that Shadow calls to notify of an event or to
 * execute destruction.
 *
 * @see #PluginFunctionTable
 */
typedef void (*PluginNotifyFunc)();

/*
 * signature for plug-in callback functions
 */
typedef void (*ShadowPluginCallbackFunc)(void* data);

/*
 * function signatures for available shadow functions
 */

/**
 * A collection of functions implemented by a plug-in. The functions
 * essentially define the plug-in interface that Shadow uses to communicate
 * with the plug-in, allowing Shadow to perform callbacks into user-specified
 * functions when creating and freeing nodes, and when a socket may be read or
 * written without blocking.
 *
 *  @param new - Pointer to a function to call when creating a new node instance.
 *  @param free - Pointer to a function to call when freeing a node instance.
 *  @param notify - Pointer to a function to call when descriptors are ready.
 */
typedef int (*ShadowRegisterFunc)(PluginNewInstanceFunc new_, PluginNotifyFunc free, PluginNotifyFunc notify);
typedef void (*ShadowLogFunc)(ShadowLogLevel level, const char* functionName, const char* format, ...);
typedef void (*ShadowCreateCallbackFunc)(ShadowPluginCallbackFunc callback, void* data, uint millisecondsDelay);
typedef int (*ShadowGetBandwidthFloorFunc)(in_addr_t ip, uint* bwdown, uint* bwup);
typedef int (*ShadowCryptoSetupFunc)(int numLocks, void** shadowLockFunc, void** shadowIdFunc, const void** shadowRandomMethod);

typedef struct _ShadowFunctionTable ShadowFunctionTable;
extern ShadowFunctionTable shadowlibFunctionTable;

/**
 * A collection of functions exported to a plug-in. Each pointer in this table
 * may be dereferenced to call a function in Shadow. Plug-ins may use these
 * functions to hook into Shadow's logging and event systems.
 */
struct _ShadowFunctionTable {
	ShadowRegisterFunc registerPlugin;
	ShadowLogFunc log;
	ShadowCreateCallbackFunc createCallback;
	ShadowGetBandwidthFloorFunc getBandwidth;
	ShadowCryptoSetupFunc cryptoSetup;
};

/* Plug-ins must implement this function to communicate with Shadow.
 * the function name symbol must be PLUGININITSYMBOL */
typedef void (*ShadowPluginInitializeFunc)(ShadowFunctionTable* shadowlibFunctions);

#endif /* SHD_LIBRARY_H_ */
