/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_H_
#define SHD_TGEN_H_

#include <glib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <shd-library.h>

typedef struct _TGenPeer TGenPeer;
struct _TGenPeer {
    in_addr_t address;
    in_port_t port;
};

#include "shd-tgen-pool.h"
#include "shd-tgen-transfer.h"
#include "shd-tgen-action.h"
#include "shd-tgen-graph.h"

#if 1 /* #ifdef DEBUG */
#define TGEN_MAGIC 0xABBABAAB
#define TGEN_ASSERT(obj) g_assert(obj && (obj->magic == TGEN_MAGIC))
#else
#define TGEN_MAGIC 0
#define TGEN_ASSERT(obj)
#endif

extern ShadowLogFunc tgenLogFunc;

#define tgen_error(...) 	if(tgenLogFunc){tgenLogFunc(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__);}
#define tgen_critical(...) 	if(tgenLogFunc){tgenLogFunc(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__);}
#define tgen_warning(...) 	if(tgenLogFunc){tgenLogFunc(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__);}
#define tgen_message(...) 	if(tgenLogFunc){tgenLogFunc(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__);}
#define tgen_info(...) 		if(tgenLogFunc){tgenLogFunc(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__);}
#define tgen_debug(...) 	if(tgenLogFunc){tgenLogFunc(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__);}

/* opaque struct containing trafficgenerator data */
typedef struct _TGen TGen;

TGen* tgen_new(gint argc, gchar* argv[], ShadowLogFunc logf, ShadowCreateCallbackFunc callf);
void tgen_free(TGen* tgen);
void tgen_activate(TGen* tgen);
gboolean tgen_hasStarted(TGen* tgen);
gboolean tgen_hasEnded(TGen* tgen);
gint tgen_getEpollDescriptor(TGen* tgen);

#endif /* SHD_TGEN_H_ */
