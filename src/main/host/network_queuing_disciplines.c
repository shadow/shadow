/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/network_queuing_disciplines.h"

#include <glib.h>
#include <stdbool.h>

#include "main/host/descriptor/compat_socket.h"
#include "main/routing/packet.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

void rrsocketqueue_init(RrSocketQueue* self) {
    utility_assert(self != NULL);
    utility_assert(self->queue == NULL);
    self->queue = g_queue_new();
}

void rrsocketqueue_destroy(RrSocketQueue* self, void (*fn_processItem)(const CompatSocket*)) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);

    if (fn_processItem != NULL) {
        while (!rrsocketqueue_isEmpty(self)) {
            CompatSocket socket;
            bool found = rrsocketqueue_pop(self, &socket);

            utility_assert(found);
            if (!found) {
                continue;
            }

            fn_processItem(&socket);
        }
    }

    g_queue_free(self->queue);
    self->queue = NULL;
}

bool rrsocketqueue_isEmpty(RrSocketQueue* self) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);
    return g_queue_is_empty(self->queue);
}

bool rrsocketqueue_pop(RrSocketQueue* self, CompatSocket* socket) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);

    uintptr_t taggedSocket = (uintptr_t)g_queue_pop_head(self->queue);
    if (taggedSocket == 0) {
        return false;
    }

    *socket = compatsocket_fromTagged(taggedSocket);
    return true;
}

void rrsocketqueue_push(RrSocketQueue* self, const CompatSocket* socket) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);
    g_queue_push_tail(self->queue, (void*)compatsocket_toTagged(socket));
}

bool rrsocketqueue_find(RrSocketQueue* self, const CompatSocket* socket) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);
    return g_queue_find(self->queue, (void*)compatsocket_toTagged(socket));
}

static gint _compareSocket(const CompatSocket* sa, const CompatSocket* sb) {
    const Packet* pa = compatsocket_peekNextOutPacket(sa);
    const Packet* pb = compatsocket_peekNextOutPacket(sb);

    utility_assert(pa != NULL);
    utility_assert(pb != NULL);

    if (pa == NULL) {
        return -1;
    }

    if (pb == NULL) {
        return +1;
    }

    return packet_getPriority(pa) > packet_getPriority(pb) ? +1 : -1;
}

static gint _compareSocketTagged(uintptr_t sa_tagged, uintptr_t sb_tagged, gpointer userData) {
    CompatSocket sa = compatsocket_fromTagged(sa_tagged);
    CompatSocket sb = compatsocket_fromTagged(sb_tagged);

    return _compareSocket(&sa, &sb);
}

void fifosocketqueue_init(FifoSocketQueue* self) {
    utility_assert(self != NULL);
    utility_assert(self->queue == NULL);
    self->queue = priorityqueue_new((GCompareDataFunc)_compareSocketTagged, NULL, NULL);
}

void fifosocketqueue_destroy(FifoSocketQueue* self, void (*fn_processItem)(const CompatSocket*)) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);

    if (fn_processItem != NULL) {
        while (!fifosocketqueue_isEmpty(self)) {
            CompatSocket socket;
            bool found = fifosocketqueue_pop(self, &socket);

            utility_assert(found);
            if (!found) {
                continue;
            }

            fn_processItem(&socket);
        }
    }

    priorityqueue_free(self->queue);
    self->queue = NULL;
}

bool fifosocketqueue_isEmpty(FifoSocketQueue* self) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);
    return priorityqueue_isEmpty(self->queue);
}

bool fifosocketqueue_pop(FifoSocketQueue* self, CompatSocket* socket) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);

    uintptr_t taggedSocket = (uintptr_t)priorityqueue_pop(self->queue);
    if (taggedSocket == 0) {
        return false;
    }

    *socket = compatsocket_fromTagged(taggedSocket);
    return true;
}

void fifosocketqueue_push(FifoSocketQueue* self, const CompatSocket* socket) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);
    priorityqueue_push(self->queue, (void*)compatsocket_toTagged(socket));
}

bool fifosocketqueue_find(FifoSocketQueue* self, const CompatSocket* socket) {
    utility_assert(self != NULL);
    utility_assert(self->queue != NULL);
    return priorityqueue_find(self->queue, (void*)compatsocket_toTagged(socket));
}
