/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_NETWORK_QUEUING_DISCIPLINES_H_
#define SRC_MAIN_HOST_NETWORK_QUEUING_DISCIPLINES_H_

#include <glib.h>
#include <stdbool.h>

#include "main/bindings/c/bindings.h"
#include "main/utility/priority_queue.h"

/* A round-robin socket queue. */
typedef struct _RrSocketQueue RrSocketQueue;
struct _RrSocketQueue {
    GQueue* queue;
};

/* A first-in-first-out socket queue. */
typedef struct _FifoSocketQueue FifoSocketQueue;
struct _FifoSocketQueue {
    PriorityQueue* queue;
    GHashTable* items;
    uint64_t push_order_counter;
};

void rrsocketqueue_init(RrSocketQueue* self);
void rrsocketqueue_destroy(RrSocketQueue* self, void (*fn_processItem)(const InetSocket*));

bool rrsocketqueue_isEmpty(RrSocketQueue* self);
bool rrsocketqueue_pop(RrSocketQueue* self, InetSocket** socket);
void rrsocketqueue_push(RrSocketQueue* self, const InetSocket* socket);
bool rrsocketqueue_find(RrSocketQueue* self, const InetSocket* socket);

void fifosocketqueue_init(FifoSocketQueue* self);
void fifosocketqueue_destroy(FifoSocketQueue* self, void (*fn_processItem)(const InetSocket*));

bool fifosocketqueue_isEmpty(FifoSocketQueue* self);
bool fifosocketqueue_pop(FifoSocketQueue* self, InetSocket** socket);
void fifosocketqueue_push(FifoSocketQueue* self, const InetSocket* socket);
bool fifosocketqueue_find(FifoSocketQueue* self, const InetSocket* socket);

#endif /* SRC_MAIN_HOST_NETWORK_QUEUING_DISCIPLINES_H_ */
