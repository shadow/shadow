/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_NETWORK_QUEUING_DISCIPLINES_H_
#define SRC_MAIN_HOST_NETWORK_QUEUING_DISCIPLINES_H_

#include <glib.h>
#include <stdbool.h>

#include "main/host/descriptor/compat_socket.h"
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
};

void rrsocketqueue_init(RrSocketQueue* self);
void rrsocketqueue_destroy(RrSocketQueue* self, void (*fn_processItem)(const CompatSocket*));

bool rrsocketqueue_isEmpty(RrSocketQueue* self);
bool rrsocketqueue_pop(RrSocketQueue* self, CompatSocket* socket);
void rrsocketqueue_push(RrSocketQueue* self, const CompatSocket* socket);
bool rrsocketqueue_find(RrSocketQueue* self, const CompatSocket* socket);

void fifosocketqueue_init(FifoSocketQueue* self);
void fifosocketqueue_destroy(FifoSocketQueue* self, void (*fn_processItem)(const CompatSocket*));

bool fifosocketqueue_isEmpty(FifoSocketQueue* self);
bool fifosocketqueue_pop(FifoSocketQueue* self, CompatSocket* socket);
void fifosocketqueue_push(FifoSocketQueue* self, const CompatSocket* socket);
bool fifosocketqueue_find(FifoSocketQueue* self, const CompatSocket* socket);

#endif /* SRC_MAIN_HOST_NETWORK_QUEUING_DISCIPLINES_H_ */
