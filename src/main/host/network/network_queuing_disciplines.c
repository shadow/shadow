/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/network/network_queuing_disciplines.h"

#include <glib.h>
#include <stdbool.h>

#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/routing/packet.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

// returns 0 if `a` and `b` represent the same object, otherwise returns 1
static gint _compareInetSocket(gconstpointer a, gconstpointer b) {
    return inetsocket_eqVoid(a, b) == false;
}

void rrsocketqueue_init(RrSocketQueue* self) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue == NULL);
    self->queue = g_queue_new();
}

void rrsocketqueue_destroy(RrSocketQueue* self, void (*fn_processItem)(const InetSocket*)) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);

    if (fn_processItem != NULL) {
        while (!rrsocketqueue_isEmpty(self)) {
            InetSocket* socket = NULL;
            bool found = rrsocketqueue_pop(self, &socket);

            utility_debugAssert(found);
            if (!found) {
                continue;
            }

            fn_processItem(socket);
        }
    }

    g_queue_free(self->queue);
    self->queue = NULL;
}

bool rrsocketqueue_isEmpty(RrSocketQueue* self) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    return g_queue_is_empty(self->queue);
}

bool rrsocketqueue_pop(RrSocketQueue* self, InetSocket** socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);

    *socket = g_queue_pop_head(self->queue);

    if (*socket == NULL) {
        return false;
    }

    return true;
}

void rrsocketqueue_push(RrSocketQueue* self, const InetSocket* socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    utility_debugAssert(socket != NULL);
    g_queue_push_tail(self->queue, (void*)socket);
}

bool rrsocketqueue_find(RrSocketQueue* self, const InetSocket* socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    return g_queue_find_custom(self->queue, (void*)socket, _compareInetSocket);
}

static gint _compareSocket(const InetSocket* sa, const InetSocket* sb) {
    uint64_t pa = 0;
    uint64_t pb = 0;

    if (inetsocket_peekNextPacketPriority(sa, &pa) != 0) {
        return -1;
    }
    if (inetsocket_peekNextPacketPriority(sb, &pb) != 0) {
        return +1;
    }

    return pa > pb ? +1 : -1;
}

void fifosocketqueue_init(FifoSocketQueue* self) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue == NULL);
    self->queue = priorityqueue_new((GCompareDataFunc)_compareSocket, NULL, NULL);
}

void fifosocketqueue_destroy(FifoSocketQueue* self, void (*fn_processItem)(const InetSocket*)) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);

    if (fn_processItem != NULL) {
        while (!fifosocketqueue_isEmpty(self)) {
            InetSocket* socket = NULL;
            bool found = fifosocketqueue_pop(self, &socket);

            utility_debugAssert(found);
            if (!found) {
                continue;
            }

            fn_processItem(socket);
        }
    }

    priorityqueue_free(self->queue);
    self->queue = NULL;
}

bool fifosocketqueue_isEmpty(FifoSocketQueue* self) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    return priorityqueue_isEmpty(self->queue);
}

bool fifosocketqueue_pop(FifoSocketQueue* self, InetSocket** socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);

    *socket = priorityqueue_pop(self->queue);
    if (socket == NULL) {
        return false;
    }

    return true;
}

void fifosocketqueue_push(FifoSocketQueue* self, const InetSocket* socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    utility_debugAssert(socket != NULL);
    gboolean successful = priorityqueue_push(self->queue, (void*)socket);
    // if this returned FALSE, it would mean that the socket was already in the queue and we would
    // need to drop the socket to avoid a memory leak
    utility_debugAssert(successful == TRUE);
}

bool fifosocketqueue_find(FifoSocketQueue* self, const InetSocket* socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    return priorityqueue_find_custom(self->queue, (void*)socket, _compareInetSocket);
}
