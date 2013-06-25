/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TORCONTROL_PINGER_H_
#define SHD_TORCONTROL_PINGER_H_

typedef struct _TorControlPinger TorControlPinger;

TorControlPinger* torcontrolpinger_new(ShadowLogFunc logFunc, ShadowCreateCallbackFunc cbFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd,
		gchar **moduleArgs, TorControl_EventHandlers *handlers);

#endif /* SHD_TORCONTROL_PINGER_H_ */
