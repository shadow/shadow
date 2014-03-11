/*
 * See LICENSE for licensing information
 */

#ifndef TORCTL_H_
#define TORCTL_H_

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include <shd-library.h>
#include <glib.h>

typedef struct _TorCTL TorCTL;

TorCTL* torctl_new(gint argc, gchar* argv[], ShadowLogFunc slogf);
void torctl_free(TorCTL* h);
void torctl_ready(TorCTL* h);
gint torctl_getEpollDescriptor(TorCTL* h);
gboolean torctl_isDone(TorCTL* h);

#endif /* TORCTL_H_ */
