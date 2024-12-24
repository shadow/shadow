/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/network/network_queuing_disciplines.h"

#include <glib.h>
#include <stdbool.h>

#include "main/bindings/c/bindings.h"
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

typedef struct _FifoItem FifoItem;
struct _FifoItem {
    uint64_t priority;
    uint64_t push_order;
};

static gint _compareSocket(const InetSocket* sa, const InetSocket* sb, FifoSocketQueue* fifo) {
    utility_debugAssert(fifo);

    FifoItem* itema = 0;
    FifoItem* itemb = 0;
    bool founda = g_hash_table_lookup_extended(fifo->items, (gpointer)sa, NULL, (gpointer*)&itema);
    bool foundb = g_hash_table_lookup_extended(fifo->items, (gpointer)sb, NULL, (gpointer*)&itemb);
    utility_debugAssert(founda && foundb);

    /*
    uint64_t pa = 0;
    uint64_t pb = 0;

    // The item->priority values were locked in at push time, but this compare function is called to
    // dynamically adjust them whenever the priority queue rebalances. This allows a socket that had
    // only bad priority packets when it was initially pushed into the queue to move up in priority
    // when it suddenly has new packets with better priority. This case can happen often in the
    // legacy TCP stack because it assigns priority=0 to control packets (ACKS) without data. This
    // means we can be more responsive to TCP controls.
    if (inetsocket_peekNextPacketPriority(sa, &pa) == 0 &&
        inetsocket_peekNextPacketPriority(sb, &pb) == 0) {
        // Typically this would be considered to violate the assumption that a queued item's
        // priority does not change while the item is enqueued!
        itema->priority = pa;
        itemb->priority = pb;
    }
    */

    if (itema->priority < itemb->priority) {
        return -1;
    } else if (itema->priority > itemb->priority) {
        return +1;
    } else if (itema->push_order < itemb->push_order) {
        return -1;
    } else if (itema->push_order > itemb->push_order) {
        return +1;
    } else {
        utility_debugAssert(false);
        return 0;
    }
}

// casts the return value from a bool to an int
static int _inetsocket_eqVoid(gconstpointer a, gconstpointer b) { return inetsocket_eqVoid(a, b); }

void fifosocketqueue_init(FifoSocketQueue* self) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue == NULL);
    utility_debugAssert(self->items == NULL);
    self->queue = priorityqueue_new(
        (GCompareDataFunc)_compareSocket, self, NULL, inetsocket_hashVoid, _inetsocket_eqVoid);
    self->items = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    self->push_order_counter = 0;
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
    g_hash_table_destroy(self->items);
    self->items = NULL;
    self->push_order_counter = 0;
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

    // The item is destroyed using g_free() as set in g_hash_table_new_full()
    g_hash_table_remove(self->items, *socket);

    return true;
}

void fifosocketqueue_push(FifoSocketQueue* self, const InetSocket* socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    utility_debugAssert(socket != NULL);

    uint64_t priority = 0;
    bool has_packet = inetsocket_peekNextPacketPriority(socket, &priority) == 0;
    utility_debugAssert(has_packet == true);

    FifoItem* item = g_new0(FifoItem, 1);
    item->priority = priority;
    item->push_order = self->push_order_counter++;
    utility_debugAssert(!g_hash_table_contains(self->items, socket));
    g_hash_table_insert(self->items, (gpointer)socket, item);

    gboolean successful = priorityqueue_push(self->queue, (void*)socket);
    // if this returned FALSE, it would mean that the socket was already in the queue and we would
    // need to drop the socket to avoid a memory leak
    utility_debugAssert(successful == TRUE);
}

bool fifosocketqueue_find(FifoSocketQueue* self, const InetSocket* socket) {
    utility_debugAssert(self != NULL);
    utility_debugAssert(self->queue != NULL);
    return priorityqueue_find(self->queue, (void*)socket);
}
