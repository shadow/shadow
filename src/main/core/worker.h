/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include <glib.h>
#include <netinet/in.h>
#include <stdbool.h>

// A pool of worker threads.
typedef struct _WorkerPool WorkerPool;
// Task to be executed on a worker thread.
typedef void (*WorkerPoolTaskFn)(void*);

#include "lib/logger/log_level.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.minimal.h"
#include "main/utility/count_down_latch.h"

#include "main/bindings/c/bindings.h"

// To be called by worker thread
void worker_finish(GQueue* hosts, SimulationTime time);

// Create a workerpool with `nThreads` threads, allowing up to `nConcurrent` to
// run at a time.
WorkerPool* workerpool_new(const Controller* controller, const ChildPidWatcher* pidWatcher,
                           Scheduler* scheduler, const ConfigOptions* config, int nWorkers,
                           int nParallel);

// Begin executing taskFn(data) on each worker thread in the pool.
void workerpool_startTaskFn(WorkerPool* pool, WorkerPoolTaskFn taskFn,
                            void* data);
// Await completion of a taskFn on every thread in the pool.
void workerpool_awaitTaskFn(WorkerPool* pool);
int workerpool_getNWorkers(WorkerPool* pool);
// Signal worker threads to exit and wait for them to do so.
void workerpool_joinAll(WorkerPool* pool);
void workerpool_free(WorkerPool* pool);
pthread_t workerpool_getThread(WorkerPool* pool, int threadId);

// Compute the global min event time across all workers. We dynamically compute
// the minimum time that we'll need for the next event round as the minimum of
// i.) all events pushed by all workers during this round, and
// ii.) the next queued event for all worker at the point when they stop
// executing events.
//
// This func is not thread safe, so only call from the scheduler thread when the
// workers are idle.
SimulationTime workerpool_getGlobalNextEventTime(WorkerPool* workerPool);

// The worker either pushed an event or finished executing its events and is
// reporting the min time of events in their event queue.
void worker_setMinEventTimeNextRound(SimulationTime simtime);

// When a new scheduling round starts, set the end time of the new round.
void worker_setRoundEndTime(SimulationTime newRoundEndTime);

int worker_getAffinity();
DNS* worker_getDNS();
const ChildPidWatcher* worker_getChildPidWatcher();
const ConfigOptions* worker_getConfig();
gboolean worker_scheduleTaskWithDelay(TaskRef* task, Host* host, SimulationTime nanoDelay);
gboolean worker_scheduleTaskAtEmulatedTime(TaskRef* task, Host* host, EmulatedTime t);
void worker_sendPacket(Host* src, Packet* packet);
bool worker_isAlive(void);
// Maximum time that the current event may run ahead to. Must only be called if we hold the host
// lock.
EmulatedTime worker_maxEventRunaheadTime(Host* host);

/* Time from the  beginning of the simulation.
 * Deprecated - prefer `worker_getCurrentEmulatedTime`.
 */
SimulationTime worker_getCurrentSimulationTime();

/* The emulated time starts at January 1st, 2000. This time should be used
 * in any places where time is returned to the application, to handle code
 * that assumes the world is in a relatively recent time. */
EmulatedTime worker_getCurrentEmulatedTime();

bool worker_isBootstrapActive(void);
guint32 worker_getNodeBandwidthUpKiBps(in_addr_t ip);
guint32 worker_getNodeBandwidthDownKiBps(in_addr_t ip);

void workerpool_updateMinHostRunahead(WorkerPool* pool, SimulationTime time);
SimulationTime worker_getLatencyForAddresses(Address* sourceAddress, Address* destinationAddress);
gdouble worker_getReliabilityForAddresses(Address* sourceAddress, Address* destinationAddress);
bool worker_isRoutableForAddresses(Address* sourceAddress, Address* destinationAddress);
void worker_incrementPacketCountForAddresses(Address* sourceAddress, Address* destinationAddress);

void worker_clearCurrentTime();
void worker_setCurrentEmulatedTime(EmulatedTime time);

gboolean worker_isFiltered(LogLevel level);

void worker_bootHosts(GQueue* hosts);
void worker_freeHosts(GQueue* hosts);

void worker_incrementPluginError();

Address* worker_resolveIPToAddress(in_addr_t ip);
Address* worker_resolveNameToAddress(const gchar* name);

// Increment a counter for the allocation of the object with the given name.
// This should be paired with an increment of the dealloc counter with the
// same name, otherwise we print a warning that a memory leak was detected.
#define worker_count_allocation(type) worker_increment_object_alloc_counter(#type)

// Increment a counter for the deallocation of the object with the given name.
// This should be paired with an increment of the alloc counter with the
// same name, otherwise we print a warning that a memory leak was detected.
#define worker_count_deallocation(type) worker_increment_object_dealloc_counter(#type)

#endif /* SHD_WORKER_H_ */
