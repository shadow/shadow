/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* thread-level storage structure */
struct _Worker {
	gint thread_id;

	Slave* slave;

	/* if single threaded, use this global event priority queue */
	EventQueue* serialEventQueue;

	SimulationTime clock_now;
	SimulationTime clock_last;
	SimulationTime clock_barrier;

	Random* random;

	Plugin* cached_plugin;
	Application* cached_application;
	Host* cached_node;
	Event* cached_event;

	GHashTable* plugins;

	MAGIC_DECLARE;
};

/* holds a thread-private key that each thread references to get a private
 * instance of a worker object */
static GPrivate workerKey = G_PRIVATE_INIT((GDestroyNotify)worker_free);
static GPrivate preloadKey = G_PRIVATE_INIT(g_free);

Worker* worker_new(Slave* slave) {
	/* make sure this isnt called twice on the same thread! */
	g_assert(!g_private_get(&workerKey));

	Worker* worker = g_new0(Worker, 1);
	MAGIC_INIT(worker);

	worker->slave = slave;
	worker->thread_id = slave_generateWorkerID(slave);
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_barrier = SIMTIME_INVALID;

	/* each worker needs a private copy of each plug-in library */
	worker->plugins = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, plugin_free);

	if(slave_getWorkerCount(slave) <= 1) {
		/* this will cause events to get pushed to this queue instead of host queues */
		worker->serialEventQueue = eventqueue_new();
	}

	g_private_replace(&workerKey, worker);
	gboolean* preloadIsReady = g_new(gboolean, 1);
	*preloadIsReady = TRUE;
	g_private_replace(&preloadKey, preloadIsReady);

	return worker;
}

void worker_free(Worker* worker) {
	MAGIC_ASSERT(worker);

	/* calls the destroy functions we specified in g_hash_table_new_full */
	g_hash_table_destroy(worker->plugins);

	if(worker->serialEventQueue) {
		eventqueue_free(worker->serialEventQueue);
	}

	MAGIC_CLEAR(worker);
	g_private_set(&workerKey, NULL);
	g_free(worker);
}

static Worker* _worker_getPrivate() {
	/* get current thread's private worker object */
	Worker* worker = g_private_get(&workerKey);
	MAGIC_ASSERT(worker);
	return worker;
}

DNS* worker_getDNS() {
	Worker* worker = _worker_getPrivate();
	return slave_getDNS(worker->slave);
}

Topology* worker_getTopology() {
	Worker* worker = _worker_getPrivate();
	return slave_getTopology(worker->slave);
}

Configuration* worker_getConfig() {
	Worker* worker = _worker_getPrivate();
	return slave_getConfig(worker->slave);
}

void worker_setKillTime(SimulationTime endTime) {
	Worker* worker = _worker_getPrivate();
	slave_setKillTime(worker->slave, endTime);
}

Plugin* worker_getPlugin(GQuark pluginID, GString* pluginPath) {
	g_assert(pluginPath);

	/* worker has a private plug-in for each plugin ID */
	Worker* worker = _worker_getPrivate();
	Plugin* plugin = g_hash_table_lookup(worker->plugins, &pluginID);
	if(!plugin) {
		/* plug-in has yet to be loaded by this worker. do that now. this call
		 * will copy the plug-in library to the temporary directory, and open
		 * that so each thread can execute in its own memory space.
		 */
		plugin = plugin_new(pluginID, pluginPath);
		g_hash_table_replace(worker->plugins, plugin_getID(plugin), plugin);
	}

	debug("worker %i using plug-in at %p", worker->thread_id, plugin);

	return plugin;
}

static guint _worker_processNode(Worker* worker, Host* node, SimulationTime barrier) {
	/* update cache, reset clocks */
	worker->cached_node = node;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_barrier = barrier;

	/* lock the node */
	host_lock(worker->cached_node);

	EventQueue* eventq = host_getEvents(worker->cached_node);
	Event* nextEvent = eventqueue_peek(eventq);

	/* process all events in the nodes local queue */
	guint nEventsProcessed = 0;
	while(nextEvent && (shadowevent_getTime(nextEvent) < worker->clock_barrier))
	{
		worker->cached_event = eventqueue_pop(eventq);

		/* make sure we don't jump backward in time */
		worker->clock_now = shadowevent_getTime(worker->cached_event);
		if(worker->clock_last != SIMTIME_INVALID) {
			g_assert(worker->clock_now >= worker->clock_last);
		}

		/* do the local task */
		gboolean complete = shadowevent_run(worker->cached_event);

		/* update times */
		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;

		/* finished event can now be destroyed */
		if(complete) {
			shadowevent_free(worker->cached_event);
			nEventsProcessed++;
		}

		/* get the next event, or NULL will tell us to break */
		nextEvent = eventqueue_peek(eventq);
	}

	/* unlock, clear cache */
	host_unlock(worker->cached_node);
	worker->cached_node = NULL;
	worker->cached_event = NULL;

	return nEventsProcessed;
}

gpointer worker_runParallel(WorkLoad* workload) {
	g_assert(workload);
	Worker* worker = worker_new(workload->slave);

	return NULL;
}

gpointer worker_runSerial(WorkLoad* workload) {
	g_assert(workload);
	Worker* worker = _worker_getPrivate();

	Event* nextEvent = eventqueue_peek(worker->serialEventQueue);
	if(nextEvent) {
		worker->clock_now = SIMTIME_INVALID;
		worker->clock_last = 0;

		/* process all events in the priority queue */
		while(nextEvent &&
			(shadowevent_getTime(nextEvent) < slave_getExecuteWindowEnd(worker->slave)) &&
			(shadowevent_getTime(nextEvent) < slave_getEndTime(worker->slave)))
		{
			/* get next event */
			nextEvent = eventqueue_pop(worker->serialEventQueue);
			worker->cached_event = nextEvent;
			worker->cached_node = shadowevent_getNode(nextEvent);

			/* ensure priority */
			worker->clock_now = shadowevent_getTime(worker->cached_event);
//			engine->clock = worker->clock_now;
			g_assert(worker->clock_now >= worker->clock_last);

			gboolean complete = shadowevent_run(worker->cached_event);
			if(complete) {
				shadowevent_free(worker->cached_event);
			}
			worker->cached_event = NULL;
			worker->cached_node = NULL;
			worker->clock_last = worker->clock_now;
			worker->clock_now = SIMTIME_INVALID;

			nextEvent = eventqueue_peek(worker->serialEventQueue);
		}
	}

	slave_setKilled(worker->slave, TRUE);

	/* in single thread mode, we must free the nodes */
	GList* hosts = workload->hosts;
	while(hosts) {
		worker->cached_node = hosts->data;
		host_freeAllApplications(worker->cached_node);
		worker->cached_node = NULL;
		hosts = hosts->next;
	}

	g_list_foreach(workload->hosts, (GFunc) host_free, NULL);

	return NULL;
}

//gpointer worker_run(WorkLoad* workload) {
//	/* get current thread's private worker object */
//	Worker* worker = _worker_getPrivate();
//
//	/* continuously run all events for this worker's assigned nodes.
//	 * the simulation is done when the engine is killed. */
//	while(!engine_isKilled(worker->cached_engine)) {
//		SimulationTime barrier = engine_getExecutionBarrier(worker->cached_engine);
//		guint nEventsProcessed = 0;
//		guint nNodesWithEvents = 0;
//
//		GSList* item = nodes;
//		while(item) {
//			Host* node = item->data;
//			guint n = _worker_processNode(worker, node, barrier);
//			nEventsProcessed += n;
//			if(n > 0) {
//				nNodesWithEvents++;
//			}
//			item = g_slist_next(item);
//		}
//
//		slave_notifyProcessed(worker->slave, nEventsProcessed, nNodesWithEvents);
//	}
//
//	/* free all applications before freeing any of the nodes since freeing
//	 * applications may cause close() to get called on sockets which needs
//	 * other node information.
//	 */
//	g_slist_foreach(nodes, (GFunc) host_freeAllApplications, NULL);
//	g_slist_foreach(nodes, (GFunc) host_free, NULL);
//
//	g_thread_exit(NULL);
//	return NULL;
//}

void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id) {
	/* TODO create accessors, or better yet refactor the work to event class */
	g_assert(event);

	/* get our thread-private worker */
	Worker* worker = _worker_getPrivate();

	/* when the event will execute */
	shadowevent_setTime(event, worker->clock_now + nano_delay);

	/* parties involved. sender may be NULL, receiver may not! */
	Host* sender = worker->cached_node;

	/* we MAY NOT OWN the receiver, so do not write to it! */
	Host* receiver = receiver_node_id == 0 ? sender : _slave_getHost(worker->slave, receiver_node_id);
	g_assert(receiver);

	/* the NodeEvent needs a pointer to the correct node */
	shadowevent_setNode(event, receiver);

	/* if we are not going to execute any more events, free it and return */
	if(slave_isKilled(worker->slave)) {
		shadowevent_free(event);
		return;
	}

	/* engine is not killed, assert accurate worker clock */
	g_assert(worker->clock_now != SIMTIME_INVALID);

	/* non-local events must be properly delayed */
	SimulationTime jump = slave_getMinTimeJump(worker->slave);
	if(!host_isEqual(receiver, sender)) {
		SimulationTime minTime = worker->clock_now + jump;

		/* warn and adjust time if needed */
		SimulationTime eventTime = shadowevent_getTime(event);
		if(eventTime < minTime) {
			debug("Inter-node event time %"G_GUINT64_FORMAT" changed to %"G_GUINT64_FORMAT" due to minimum delay %"G_GUINT64_FORMAT,
					eventTime, minTime, jump);
			shadowevent_setTime(event, minTime);
		}
	}

	/* figure out where to push the event */
	if(worker->serialEventQueue) {
		/* single-threaded, push to global serial queue */
		eventqueue_push(worker->serialEventQueue, event);
	} else {
		/* multi-threaded, push event to receiver node */
		EventQueue* eventq = host_getEvents(receiver);
		eventqueue_push(eventq, event);
	}
}

void worker_scheduleRetransmit(Packet* packet) {
	/* source should retransmit. use latency to approximate RTT for 'retransmit timer' */
	in_addr_t srcIP = packet_getSourceIP(packet);
	in_addr_t dstIP = packet_getDestinationIP(packet);

	Address* srcAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32) srcIP);
	Address* dstAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32) dstIP);

	PacketDroppedEvent* event = packetdropped_new(packet);

	SimulationTime delay = 0;
	if(address_isLocal(srcAddress) || address_isLocal(dstAddress)) {
		g_assert(address_isLocal(srcAddress));
		g_assert(address_isLocal(dstAddress));
		delay = 1;
	} else {
		gdouble latency = topology_getLatency(worker_getTopology(), srcAddress, dstAddress);
		delay = (SimulationTime) floor(latency * SIMTIME_ONE_MILLISECOND);
	}

	worker_scheduleEvent((Event*)event, delay, (GQuark) address_getID(srcAddress));
}

void worker_schedulePacket(Packet* packet) {
	in_addr_t srcIP = packet_getSourceIP(packet);
	in_addr_t dstIP = packet_getDestinationIP(packet);

	Address* srcAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32) srcIP);
	Address* dstAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32) dstIP);

    /* XXX FIXME this needs to be fixed! */
	if(!srcAddress || !dstAddress) {
	    critical("unable to schedule packet because of null addresses, ignoring");
		return;
    }

	/* first thing to check is if network reliability forces us to 'drop'
	 * the packet. if so, get out of dodge doing as little as possible. */
	gdouble reliability = topology_getReliability(worker_getTopology(), srcAddress, dstAddress);
	Random* random = host_getRandom(worker_getCurrentHost());
	gdouble chance = random_nextDouble(random);

	if(chance > reliability){
		/* sender side is scheduling packets, but we are simulating
		 * the packet being dropped between sender and receiver, so
		 * it will need to be retransmitted */
		worker_scheduleRetransmit(packet);
	} else {
		/* packet will make it through, find latency */
		gdouble latency = topology_getLatency(worker_getTopology(), srcAddress, dstAddress);
		SimulationTime delay = (SimulationTime) floor(latency * SIMTIME_ONE_MILLISECOND);

		PacketArrivedEvent* event = packetarrived_new(packet);
		worker_scheduleEvent((Event*)event, delay, (GQuark)address_getID(dstAddress));
	}
}

gboolean worker_isAlive() {
	return g_private_get(&workerKey) != NULL;
}

gboolean worker_isInShadowContext() {
	/* this must return TRUE while destroying the thread pool to avoid
	 * calling worker_getPrivate (which messes with threads) while trying to
	 * shutdown the threads.
	 */
//	if(shadowMaster && !(slave_isForced(shadowMaster))) {
	if(g_private_get(&workerKey)) {
		Worker* worker = _worker_getPrivate();
		if(worker->cached_plugin) {
			return plugin_isShadowContext(worker->cached_plugin);
		}
	}
//	}
	/* if there is no engine or cached plugin, we are definitely in Shadow context */
	return TRUE;
}

Host* worker_getCurrentHost() {
	Worker* worker = _worker_getPrivate();
	return worker->cached_node;
}

Application* worker_getCurrentApplication() {
	Worker* worker = _worker_getPrivate();
	return worker->cached_application;
}

void worker_setCurrentApplication(Application* application) {
	Worker* worker = _worker_getPrivate();
	worker->cached_application = application;
}

Plugin* worker_getCurrentPlugin() {
	Worker* worker = _worker_getPrivate();
	return worker->cached_plugin;
}

void worker_setCurrentPlugin(Plugin* plugin) {
	Worker* worker = _worker_getPrivate();
	worker->cached_plugin = plugin;
}

SimulationTime worker_getCurrentTime() {
	Worker* worker = _worker_getPrivate();
	return worker->clock_now;
}

guint worker_getRawCPUFrequency() {
	Worker* worker = _worker_getPrivate();
	return slave_getRawCPUFrequency(worker->slave);
}

gdouble worker_nextRandomDouble() {
	Worker* worker = _worker_getPrivate();
	return slave_nextRandomDouble(worker->slave);
}

gint worker_nextRandomInt() {
	Worker* worker = _worker_getPrivate();
	return slave_nextRandomInt(worker->slave);
}

void worker_lockPluginInit() {
	Worker* worker = _worker_getPrivate();
	slave_lockPluginInit(worker->slave);
}

void worker_unlockPluginInit() {
	Worker* worker = _worker_getPrivate();
	slave_unlockPluginInit(worker->slave);
}

guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip) {
	Worker* worker = _worker_getPrivate();
	return slave_getNodeBandwidthUp(worker->slave, nodeID, ip);
}

guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip) {
	Worker* worker = _worker_getPrivate();
	return slave_getNodeBandwidthDown(worker->slave, nodeID, ip);
}

gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID) {
	Worker* worker = _worker_getPrivate();
	return slave_getLatency(worker->slave, sourceNodeID, destinationNodeID);
}

void worker_addHost(Host* host, guint hostID) {
	Worker* worker = _worker_getPrivate();
	slave_addHost(worker->slave, host, hostID);
}

void worker_cryptoLockingFunc(gint mode, gint n) {
	Worker* worker = _worker_getPrivate();
	slave_cryptoLockingFunc(worker->slave, mode, n);
}

gboolean worker_cryptoSetup(gint numLocks) {
	Worker* worker = _worker_getPrivate();
	return slave_cryptoSetup(worker->slave, numLocks);
}

gint worker_getThreadID() {
	Worker* worker = _worker_getPrivate();
	return worker->thread_id;
}

void worker_storePluginPath(GQuark pluginID, const gchar* pluginPath) {
	Worker* worker = _worker_getPrivate();
	slave_storePluginPath(worker->slave, pluginID, pluginPath);
}

const gchar* worker_getPluginPath(GQuark pluginID) {
	Worker* worker = _worker_getPrivate();
	return slave_getPluginPath(worker->slave, pluginID);
}

void worker_setTopology(Topology* topology) {
	Worker* worker = _worker_getPrivate();
	return slave_setTopology(worker->slave, topology);
}

GTimer* worker_getRunTimer() {
	Worker* worker = _worker_getPrivate();
	return slave_getRunTimer(worker->slave);
}

void worker_setCurrentTime(SimulationTime time) {
	Worker* worker = _worker_getPrivate();
	worker->clock_now = time;
}
