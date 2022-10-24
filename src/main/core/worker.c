/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/core/worker.h"

#include <glib.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

CEmulatedTime worker_maxEventRunaheadTime(const Host* host) {
    utility_debugAssert(host);
    CEmulatedTime max = emutime_add_simtime(EMUTIME_SIMULATION_START, _worker_getRoundEndTime());

    CEmulatedTime nextEventTime = host_nextEventTime(host);
    if (nextEventTime != 0) {
        max = MIN(max, nextEventTime);
    }

    return max;
}

// TODO: move to Router::_route_incoming_packet
static void _worker_runDeliverPacketTask(const Host* host, gpointer voidPacket, gpointer userData) {
    Packet* packet = voidPacket;
    Router* router = host_getUpstreamRouter(host);
    utility_debugAssert(router != NULL);

    // Keep ownership of the packet after the task unrefs it.
    packet_ref(packet);
    // Ownership transfers to the router here.
    bool became_nonempty = router_enqueue(router, packet);

    if (became_nonempty) {
        networkinterface_receivePackets(host_lookupInterface(host, host_getDefaultIP(host)), host);
    }
}

// TODO: move to Router::_route_outgoing_packet
void worker_sendPacket(const Host* srcHost, Packet* packet) {
    utility_debugAssert(packet != NULL);

    if (worker_isSimCompleted()) {
        /* the simulation is over, don't bother */
        return;
    }

    in_addr_t srcIP = packet_getSourceIP(packet);
    in_addr_t dstIP = packet_getDestinationIP(packet);

    const Address* dstAddress = worker_resolveIPToAddress(dstIP);

    if (!dstAddress) {
        utility_panic("unable to schedule packet because of null address");
        return;
    }

    gboolean bootstrapping = worker_isBootstrapActive();

    /* check if network reliability forces us to 'drop' the packet */
    gdouble reliability = worker_getReliability(srcIP, dstIP);

    Random* random = host_getRandom(srcHost);
    gdouble chance = random_nextDouble(random);

    /* don't drop control packets with length 0, otherwise congestion
     * control has problems responding to packet loss */
    if (bootstrapping || chance <= reliability || packet_getPayloadSize(packet) == 0) {
        /* the sender's packet will make it through, find latency */
        CSimulationTime delay = worker_getLatency(srcIP, dstIP);
        worker_updateLowestUsedLatency(delay);
        CSimulationTime deliverTime = worker_getCurrentSimulationTime() + delay;

        worker_incrementPacketCount(srcIP, dstIP);

        /* TODO this should change for sending to remote manager (on a different machine)
         * this is the only place where tasks are sent between separate hosts */

        HostId dstHostID = address_getID(dstAddress);

        packet_addDeliveryStatus(packet, PDS_INET_SENT);

        /* the packetCopy starts with 1 ref, which will be held by the packet task
         * and unreffed after the task is finished executing. */
        Packet* packetCopy = packet_copy(packet);

        /* Safe to use the "unbound" constructor here, since there are no other references
         * to `packetCopy`.
         */
        TaskRef* packetTask = taskref_new_unbound(
            _worker_runDeliverPacketTask, packetCopy, NULL, (TaskObjectFreeFunc)packet_unref, NULL);

        Event* packetEvent = event_new(packetTask, deliverTime, srcHost, dstHostID);

        taskref_drop(packetTask);

        CSimulationTime roundEndTime = _worker_getRoundEndTime();

        // delay the packet until the next round
        if (deliverTime < roundEndTime) {
            event_setTime(packetEvent, roundEndTime);
        }

        // we may have sent this packet after the destination host finished running the current
        // round and calculated its min event time, so we put this in our min event time instead
        worker_setMinEventTimeNextRound(event_getTime(packetEvent));

        worker_pushToHost(dstHostID, packetEvent);
    } else {
        packet_addDeliveryStatus(packet, PDS_INET_DROPPED);
    }
}
