/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TORCONTROL_LOGGER_H_
#define SHD_TORCONTROL_LOGGER_H_

typedef struct _TorControlLogger TorControlLogger;

TorControlLogger* torcontrollogger_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd,
		gchar **moduleArgs, TorControl_EventHandlers *handlers);

#endif /* SHD_TORCONTROL_LOGGER_H_ */
