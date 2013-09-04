/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_NOTIFY_PLUGIN_H_
#define SHD_NOTIFY_PLUGIN_H_

#include "shadow.h"

typedef struct _NotifyPluginEvent NotifyPluginEvent;

NotifyPluginEvent* notifyplugin_new(gint epollHandle);
void notifyplugin_run(NotifyPluginEvent* event, Host* node);
void notifyplugin_free(NotifyPluginEvent* event);

#endif /* SHD_NOTIFY_PLUGIN_H_ */
