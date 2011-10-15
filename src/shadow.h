/**
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

#ifndef SHADOW_H_
#define SHADOW_H_

#include <glib.h>
#include <gmodule.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "shd-config.h"

/*
 * order of includes is very important to prevent circular dependencies.
 * place base classes with few dependencies first.
 */

#include "engine/shd-main.h"

typedef struct vudp_s vudp_t, *vudp_tp;
typedef struct vtransport_s vtransport_t, *vtransport_tp;
typedef struct vtransport_item_s vtransport_item_t, *vtransport_item_tp;
typedef struct vtransport_mgr_inq_s vtransport_mgr_inq_t, *vtransport_mgr_inq_tp;
typedef struct vtransport_mgr_s vtransport_mgr_t, *vtransport_mgr_tp;
typedef struct vtcp_s vtcp_t, *vtcp_tp;
typedef struct vtcp_server_child_s vtcp_server_child_t, *vtcp_server_child_tp;
typedef struct vtcp_server_s vtcp_server_t, *vtcp_server_tp;
typedef struct vinterface_s vinterface_t, *vinterface_tp;
typedef struct vsocket_t vsocket_t, *vsocket_tp;
typedef struct vsocket_mgr_s vsocket_mgr_t, *vsocket_mgr_tp;
typedef struct vpipe_unid_s vpipe_unid_t, *vpipe_unid_tp;
typedef struct vpipe_bid_s vpipe_bid_t, *vpipe_bid_tp;
typedef struct vpipe_mgr_s vpipe_mgr_t, *vpipe_mgr_tp;
typedef struct vpeer_s vpeer_t, *vpeer_tp;
typedef struct vpacket_tcp_header_s vpacket_tcp_header_t, *vpacket_tcp_header_tp;
typedef struct vpacket_header_s vpacket_header_t, *vpacket_header_tp;
typedef struct vpacket_s vpacket_t, *vpacket_tp;
typedef struct vpacket_pod_s vpacket_pod_t, *vpacket_pod_tp;
typedef struct rc_vpacket_pod_s rc_vpacket_pod_t, *rc_vpacket_pod_tp;
typedef struct vpacket_mgr_s vpacket_mgr_t, *vpacket_mgr_tp;
typedef struct vevent_socket_s vevent_socket_t, *vevent_socket_tp;
typedef struct vevent_s vevent_t, *vevent_tp;
typedef struct vevent_timer_s vevent_timer_t, *vevent_timer_tp;
typedef struct vevent_base_s vevent_base_t, *vevent_base_tp;
typedef struct vevent_mgr_s vevent_mgr_t, *vevent_mgr_tp;
typedef struct vepoll_s vepoll_t, *vepoll_tp;
typedef struct vcpu_s vcpu_t, *vcpu_tp;
typedef struct vbuffer_sbuf_s vbuffer_sbuf_t, *vbuffer_sbuf_tp;
typedef struct vbuffer_rbuf_s vbuffer_rbuf_t, *vbuffer_rbuf_tp;
typedef struct vbuffer_s vbuffer_t, *vbuffer_tp;

/* configuration, base runnables, and input parsing */
#include "configuration/shd-configuration.h"
#include "runnable/shd-runnable.h"
#include "runnable/event/shd-event.h"
#include "runnable/action/shd-action.h"
#include "configuration/shd-parser.h"

/* utilities with limited dependencies */
#include "utility/shd-registry.h"
#include "utility/shd-cdf.h"
#include "utility/shd-data-entry.h"

#include "runnable/event/shd-callback.h"
#include "runnable/event/shd-packet-arrived.h"
#include "runnable/event/shd-packet-received.h"
#include "runnable/event/shd-packet-sent.h"
#include "runnable/event/shd-socket-activated.h"
#include "runnable/event/shd-socket-poll-timer-expired.h"
#include "runnable/event/shd-start-application.h"
#include "runnable/event/shd-spine.h"
#include "runnable/event/shd-tcp-close-timer-expired.h"
#include "runnable/event/shd-tcp-dack-timer-expired.h"
#include "runnable/event/shd-tcp-retransmit-timer-expired.h"
#include "runnable/action/shd-connect-network.h"
#include "runnable/action/shd-create-software.h"
#include "runnable/action/shd-create-network.h"
#include "runnable/action/shd-create-node.h"
#include "runnable/action/shd-generate-cdf.h"
#include "runnable/action/shd-kill-engine.h"
#include "runnable/action/shd-load-cdf.h"
#include "runnable/action/shd-load-plugin.h"
#include "runnable/action/shd-spina.h"

#include "plugin/libraries/shadowlib.h"
#include "plugin/shd-shadowlib.h"
#include "plugin/shd-plugin-state.h"
#include "plugin/shd-plugin.h"
#include "plugin/shd-software.h"

#include "topology/shd-address.h"
#include "topology/shd-network.h"
#include "topology/shd-link.h"
#include "topology/shd-internetwork.h"

#include "node/shd-application.h"
#include "node/shd-node.h"

#include "engine/shd-logging.h"
#include "engine/shd-engine.h"
#include "engine/shd-worker.h"

/* make libevent types slightly prettier */
typedef struct event event_t, *event_tp;
typedef struct event_base event_base_t, *event_base_tp;
typedef struct evdns_base evdns_base_t, *evdns_base_tp;
typedef struct evdns_request evdns_request_t, *evdns_request_tp;
typedef struct evdns_server_request evdns_server_request_t, *evdns_server_request_tp;
typedef struct evdns_server_port evdns_server_port_t, *evdns_server_port_tp;

#include "utility/rand.h"
#include "utility/linkedbuffer.h"
#include "utility/orderedlist.h"
#include "utility/reference_counter.h"
#include "virtual/vbuffer.h"
#include "virtual/vcpu.h"
#include "virtual/vepoll.h"
#include "virtual/vevent.h"
#include "virtual/vevent_mgr.h"
#include "virtual/vpacket.h"
#include "virtual/vpacket_mgr.h"
#include "virtual/vpeer.h"
#include "virtual/vpipe.h"
#include "virtual/vsocket_mgr_server.h"
#include "virtual/vsocket_mgr.h"
#include "virtual/vsocket.h"
#include "virtual/vsystem.h"
#include "virtual/vtcp_server.h"
#include "virtual/vtcp.h"
#include "virtual/vtransport_processing.h"
#include "virtual/vtransport_mgr.h"
#include "virtual/vtransport.h"
#include "virtual/vudp.h"

#include "intercept/preload.h"
#include "intercept/intercept.h"
#include "intercept/vcrypto_intercept.h"
#include "intercept/vevent_intercept.h"
#include "intercept/vsocket_intercept.h"
#include "intercept/vsystem_intercept.h"

extern Engine* shadow_engine;

#endif /* SHADOW_H_ */
