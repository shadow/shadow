/*
 * shd-router-queue-codel.c
 *
 *  An active queue management (AQM) algorithm implementing CoDel.
 *  https://tools.ietf.org/html/rfc8289
 *
 *  The "Flow Queue" variant is not implemented.
 *  https://tools.ietf.org/html/rfc8290
 *
 *  More info:
 *   - https://en.wikipedia.org/wiki/CoDel
 *   - http://man7.org/linux/man-pages/man8/tc-codel.8.html
 *   - https://queue.acm.org/detail.cfm?id=2209336
 *   - https://queue.acm.org/appendices/codel.html
 *
 *  Created on: Jan 9, 2020
 *      Author: rjansen
 */

#include "routing/shd-router-queue-codel.h"

#include <glib.h>
#include <math.h>
#include <stddef.h>

#include "core/logger/shd-logger.h"
#include "core/shd-worker.h"
#include "core/support/shd-definitions.h"
#include "routing/shd-packet.h"
#include "routing/shd-router.h"
#include "utility/shd-utility.h"

/* hard limit of queue size, in number of packets. this is recommended to be
 * 1000 in normal routers, but in Shadow we don't enforce a practical limit.
 * this corresponds to the "LIMIT" parameter in the RFC. */
#define CODEL_PARAM_QUEUE_SIZE_LIMIT G_MAXUINT

/* target minimum standing queue delay time. this is recommended to be
 * set to 5 milliseconds, but in Shadow we increase it to 10 milliseconds.
 * this corresponds to the "TARGET" parameter in the RFC.
 * note that the raw value is in SimTime, i.e., number of nanoseconds. */
#define CODEL_PARAM_TARGET_DELAY_SIMTIME (10*SIMTIME_ONE_MILLISECOND)

/* delay is computed over the most recent interval time. we follow the
 * recommended setting of 100 milliseconds. this corresponds to the
 * "INTERVAL" parameter in the RFC. note that the raw value is in SimTime,
 * i.e., number of nanoseconds.*/
#define CODEL_PARAM_INTERVAL_SIMTIME (100*SIMTIME_ONE_MILLISECOND)

typedef enum _CoDelMode CoDelMode;
enum _CoDelMode {
    CODEL_MODE_STORE, // under good conditions, we store and forward packets
    CODEL_MODE_DROP, // under bad conditions, we occasionally drop packets
};

typedef struct _CoDelEntry CoDelEntry;
struct _CoDelEntry {
    Packet* packet;
    SimulationTime enqueueTS;
};

typedef struct _QueueManagerCoDel QueueManagerCoDel;
struct _QueueManagerCoDel {
    /* the queue holding the packets and timestamps */
    GQueue* entries;
    /* total amount of bytes stored */
    guint64 totalSize;

    /* if we are in dropping mode or not */
    CoDelMode mode;
    /* if nonzero, this is an interval worth of time after delays rose above target */
    SimulationTime intervalExpireTS;
    /* the next time we should drop a packet */
    SimulationTime nextDropTS;
    /* number of packets dropped since entering drop mode */
    guint dropCount;
    guint dropCountLast;

};

static QueueManagerCoDel* _routerqueuecodel_new() {
    QueueManagerCoDel* queueManager = g_new0(QueueManagerCoDel, 1);

    queueManager->mode = CODEL_MODE_STORE;
    queueManager->entries = g_queue_new();

    return queueManager;
}

static void _routerqueuecodel_free(QueueManagerCoDel* queueManager) {
    utility_assert(queueManager);

    if(queueManager->entries) {
        while(!g_queue_is_empty(queueManager->entries)) {
            CoDelEntry* entry = g_queue_pop_head(queueManager->entries);
            if(entry) {
                if(entry->packet) {
                    packet_unref(entry->packet);
                }
                g_free(entry);
            }
        }
        g_queue_free(queueManager->entries);
    }

    g_free(queueManager);
}

static inline guint64 _routerqueuecodel_getPacketLength(Packet* packet) {
    return (guint64)(packet_getPayloadLength(packet) + packet_getHeaderSize(packet));
}

static gboolean _routerqueuecodel_enqueue(QueueManagerCoDel* queueManager, Packet* packet) {
    utility_assert(queueManager);
    utility_assert(packet);

    if(g_queue_get_length(queueManager->entries) < CODEL_PARAM_QUEUE_SIZE_LIMIT) {
        /* we will store the packet */
        packet_ref(packet);

        CoDelEntry* entry = g_new0(CoDelEntry, 1);
        entry->packet = packet;
        entry->enqueueTS = worker_getCurrentTime();
        g_queue_push_tail(queueManager->entries, entry);

        guint64 length = _routerqueuecodel_getPacketLength(packet);
        queueManager->totalSize += length;

        return TRUE;
    } else {
        /* we already have reached our hard packet limit, so we drop it */
        // TODO not sure if we need to change to drop mode or count this as a dropped packet
        // XXX Until CODEL_PARAM_QUEUE_SIZE_LIMIT becomes less than infinity, it does not matter
        return FALSE;
    }
}

static void _routerqueuecodel_drop(Packet* packet) {
    packet_addDeliveryStatus(packet, PDS_ROUTER_DROPPED);
#ifdef DEBUG
    gchar* pString = packet_toString(packet);
    debug("Router dropped packet %s", pString);
    g_free(pString);
#endif
    packet_unref(packet);
}

static Packet* _routerqueuecodel_dequeueHelper(QueueManagerCoDel* queueManager,
        SimulationTime now, gboolean* okToDrop) {
    *okToDrop = FALSE;
    Packet* packet = NULL;
    SimulationTime ts = 0;

    CoDelEntry* entry = g_queue_pop_head(queueManager->entries);
    if(entry) {
        packet = entry->packet;
        ts = entry->enqueueTS;
        g_free(entry);
    }

    if(packet == NULL) {
        /* queue is empty, we cannot be above target.
         * reset the interval expiration */
        queueManager->intervalExpireTS = 0;
        return NULL;
    }

    guint64 length = _routerqueuecodel_getPacketLength(packet);
    utility_assert(length <= queueManager->totalSize);
    queueManager->totalSize -= length;

    utility_assert(now >= ts);
    SimulationTime sojournTime = now - ts;

    if(sojournTime < CODEL_PARAM_TARGET_DELAY_SIMTIME || queueManager->totalSize < CONFIG_MTU) {
        /* We are in a good state, i.e., below the target delay. We reset the interval
         * expiration, so that we wait for at least interval if the delay exceeds the
         * target again. */
        queueManager->intervalExpireTS = 0;
    } else {
        /* We are in a bad state, i.e., at or above the target delay. */
        if(queueManager->intervalExpireTS == 0) {
            /* We were in a good state and just entered a bad state. If we stay in the
             * bad state for a full interval, we enter drop mode. */
            queueManager->intervalExpireTS = now + CODEL_PARAM_INTERVAL_SIMTIME;
        } else {
            /* We were already in a bad state and stayed in it. If we have been in it
             * for a full interval worth of time, then we drop this packet. */
            if(now >= queueManager->intervalExpireTS) {
                *okToDrop = TRUE;
            }
        }
    }

    return packet;
}

static SimulationTime _routerqueuecodel_controlLaw(guint count, SimulationTime ts) {
    SimulationTime newTS = ts + CODEL_PARAM_INTERVAL_SIMTIME;

    double result = ((double)newTS) / sqrt((double)count);
    double rounded = round(result);

    return (SimulationTime) rounded;
}

static Packet* _routerqueuecodel_dequeue(QueueManagerCoDel* queueManager) {
    utility_assert(queueManager);

    SimulationTime now = worker_getCurrentTime();

    gboolean okToDrop = FALSE;
    Packet* packet = _routerqueuecodel_dequeueHelper(queueManager, now, &okToDrop);

    /* If we have an empty queue, we exit dropping state. */
    if(packet == NULL) {
        queueManager->mode = CODEL_MODE_STORE;
        return packet;
    }

    if(queueManager->mode == CODEL_MODE_DROP) {
        if(!okToDrop) {
            /* delays are low again, leave drop mode */
            queueManager->mode = CODEL_MODE_STORE;
        }

        while(now >= queueManager->nextDropTS && queueManager->mode == CODEL_MODE_DROP) {
            /* drop the packet */
            _routerqueuecodel_drop(packet);
            queueManager->dropCount++;

            /* get the next one */
            packet = _routerqueuecodel_dequeueHelper(queueManager, now, &okToDrop);

            if(okToDrop) {
                /* schedule the next drop */
                queueManager->nextDropTS = _routerqueuecodel_controlLaw(queueManager->dropCount, queueManager->nextDropTS);
            } else {
                queueManager->mode = CODEL_MODE_STORE;
            }
        }
    } else if(okToDrop) {
        /* We are in storing mode, but we should now drop this packet. */
        _routerqueuecodel_drop(packet);

        /* get the next one */
        packet = _routerqueuecodel_dequeueHelper(queueManager, now, &okToDrop);

        /* turn on dropping mode */
        queueManager->mode = CODEL_MODE_DROP;

        /* reset to the drop rate that was known to control the queue */
        guint delta = queueManager->dropCount - queueManager->dropCountLast;
        queueManager->dropCount = 1;

        gboolean droppingRecently = (now < queueManager->nextDropTS + (16*CODEL_PARAM_INTERVAL_SIMTIME)) ? TRUE : FALSE;

        if(droppingRecently && delta > 1) {
            queueManager->dropCount = delta;
        }

        queueManager->nextDropTS = _routerqueuecodel_controlLaw(queueManager->dropCount, now);
        queueManager->dropCountLast = queueManager->dropCount;
    }

    return packet;
}

static Packet* _routerqueuecodel_peek(QueueManagerCoDel* queueManager) {
    utility_assert(queueManager);

    CoDelEntry* entry = g_queue_peek_head(queueManager->entries);

    if(entry && entry->packet) {
        return entry->packet;
    } else {
        return NULL;
    }
}

static const struct _QueueManagerHooks _routerqueuecodel_hooks = {
    .new = (QueueManagerNew) _routerqueuecodel_new,
    .free = (QueueManagerFree) _routerqueuecodel_free,
    .enqueue = (QueueManagerEnqueue) _routerqueuecodel_enqueue,
    .dequeue = (QueueManagerDequeue) _routerqueuecodel_dequeue,
    .peek = (QueueManagerPeek) _routerqueuecodel_peek
};

const QueueManagerHooks* routerqueuecodel_getHooks() {
    return &_routerqueuecodel_hooks;
}
