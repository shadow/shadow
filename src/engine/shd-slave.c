/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Slave {
	Master* master;

	/* the worker object associated with the main thread of execution */
	Worker* mainThreadWorker;

	/* simulation configuration options */
	Configuration* config;

	/* slave random source, init from master random, used to init host randoms */
	Random* random;

	/* network connectivity */
	Topology* topology;
	DNS* dns;

	/* virtual hosts */
	GHashTable* hosts;

	GHashTable* pluginPaths;

	/* if multi-threaded, we use worker thread */
	CountDownLatch* processingLatch;
	CountDownLatch* barrierLatch;

	/* openssl needs us to manage locking */
	GMutex* cryptoThreadLocks;
	gint numCryptoThreadLocks;

	guint nWorkers;

	/* id generation counters, must be protected for thread safety */
	volatile gint workerIDCounter;

	GMutex lock;
	GMutex pluginInitLock;

	gint rawFrequencyKHz;
	guint numEventsCurrentInterval;
	guint numNodesWithEventsCurrentInterval;

	/* We will not enter plugin context when set. Used when destroying threads */
	gboolean forceShadowContext;

	MAGIC_DECLARE;
};

static void _slave_lock(Slave* slave) {
	MAGIC_ASSERT(slave);
	g_mutex_lock(&(slave->lock));
}

static void _slave_unlock(Slave* slave) {
	MAGIC_ASSERT(slave);
	g_mutex_unlock(&(slave->lock));
}

// TODO make this static
Host* _slave_getHost(Slave* slave, GQuark hostID) {
	MAGIC_ASSERT(slave);
	return (Host*) g_hash_table_lookup(slave->hosts, GUINT_TO_POINTER((guint)hostID));
}

void slave_addHost(Slave* slave, Host* host, guint hostID) {
	MAGIC_ASSERT(slave);
	g_hash_table_replace(slave->hosts, GUINT_TO_POINTER(hostID), host);
}

static GList* _slave_getAllHosts(Slave* slave) {
	MAGIC_ASSERT(slave);
	return g_hash_table_get_values(slave->hosts);
}

Slave* slave_new(Master* master, Configuration* config, guint randomSeed) {
	Slave* slave = g_new0(Slave, 1);
	MAGIC_INIT(slave);

	g_mutex_init(&(slave->lock));
	g_mutex_init(&(slave->pluginInitLock));

	slave->master = master;
	slave->config = config;
	slave->random = random_new(randomSeed);

	slave->rawFrequencyKHz = utility_getRawCPUFrequency(CONFIG_CPU_MAX_FREQ_FILE);
	if(slave->rawFrequencyKHz == 0) {
		info("unable to read '%s' for copying", CONFIG_CPU_MAX_FREQ_FILE);
	}

	slave->hosts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
	slave->pluginPaths = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);

	slave->dns = dns_new();

	slave->nWorkers = (guint) configuration_getNWorkerThreads(config);
	slave->mainThreadWorker = worker_new(slave);

	return slave;
}

void slave_free(Slave* slave) {
	MAGIC_ASSERT(slave);

	/* this launches delete on all the plugins and should be called before
	 * the engine is marked "killed" and workers are destroyed.
	 */
	g_hash_table_destroy(slave->hosts);

	/* we will never execute inside the plugin again */
	slave->forceShadowContext = TRUE;

	if(slave->topology) {
		topology_free(slave->topology);
	}
	if(slave->dns) {
		dns_free(slave->dns);
	}

	g_hash_table_destroy(slave->pluginPaths);

	for(int i = 0; i < slave->numCryptoThreadLocks; i++) {
		g_mutex_clear(&(slave->cryptoThreadLocks[i]));
	}

	g_mutex_clear(&(slave->lock));
	g_mutex_clear(&(slave->pluginInitLock));

	/* join and free spawned worker threads */
//TODO

	/* free main worker */
	worker_free(slave->mainThreadWorker);

	MAGIC_CLEAR(slave);
	g_free(slave);
}

gboolean slave_isForced(Slave* slave) {
	MAGIC_ASSERT(slave);
	return slave->forceShadowContext;
}

guint slave_getRawCPUFrequency(Slave* slave) {
	MAGIC_ASSERT(slave);
	_slave_lock(slave);
	guint freq = slave->rawFrequencyKHz;
	_slave_unlock(slave);
	return freq;
}

gint slave_nextRandomInt(Slave* slave) {
	MAGIC_ASSERT(slave);
	_slave_lock(slave);
	gint r = random_nextInt(slave->random);
	_slave_unlock(slave);
	return r;
}

gdouble slave_nextRandomDouble(Slave* slave) {
	MAGIC_ASSERT(slave);
	_slave_lock(slave);
	gdouble r = random_nextDouble(slave->random);
	_slave_unlock(slave);
	return r;
}

GTimer* slave_getRunTimer(Slave* slave) {
	return master_getRunTimer(slave->master);
}

gint slave_generateWorkerID(Slave* slave) {
	MAGIC_ASSERT(slave);
	_slave_lock(slave);
	gint id = slave->workerIDCounter;
	(slave->workerIDCounter)++;
	_slave_unlock(slave);
	return id;
}

void slave_storePluginPath(Slave* slave, GQuark pluginID, const gchar* pluginPath) {
	MAGIC_ASSERT(slave);
	GQuark* key = g_new0(GQuark, 1);
	*key = pluginID;
	gchar* value = g_strdup(pluginPath);
	g_hash_table_insert(slave->pluginPaths, key, value);
}

const gchar* slave_getPluginPath(Slave* slave, GQuark pluginID) {
	MAGIC_ASSERT(slave);
	return g_hash_table_lookup(slave->pluginPaths, &pluginID);
}

void slave_lockPluginInit(Slave* slave) {
	MAGIC_ASSERT(slave);
	g_mutex_lock(&(slave->pluginInitLock));
}

void slave_unlockPluginInit(Slave* slave) {
	MAGIC_ASSERT(slave);
	g_mutex_unlock(&(slave->pluginInitLock));
}

DNS* slave_getDNS(Slave* slave) {
	MAGIC_ASSERT(slave);
	return slave->dns;
}

Topology* slave_getTopology(Slave* slave) {
	MAGIC_ASSERT(slave);
	return slave->topology;
}

void slave_setTopology(Slave* slave, Topology* topology) {
	MAGIC_ASSERT(slave);
	slave->topology = topology;
}

guint32 slave_getNodeBandwidthUp(Slave* slave, GQuark nodeID, in_addr_t ip) {
	MAGIC_ASSERT(slave);
	Host* host = _slave_getHost(slave, nodeID);
	NetworkInterface* interface = host_lookupInterface(host, ip);
	return networkinterface_getSpeedUpKiBps(interface);
}

guint32 slave_getNodeBandwidthDown(Slave* slave, GQuark nodeID, in_addr_t ip) {
	MAGIC_ASSERT(slave);
	Host* host = _slave_getHost(slave, nodeID);
	NetworkInterface* interface = host_lookupInterface(host, ip);
	return networkinterface_getSpeedDownKiBps(interface);
}

gdouble slave_getLatency(Slave* slave, GQuark sourceNodeID, GQuark destinationNodeID) {
	MAGIC_ASSERT(slave);
	Host* sourceNode = _slave_getHost(slave, sourceNodeID);
	Host* destinationNode = _slave_getHost(slave, destinationNodeID);
	Address* sourceAddress = host_getDefaultAddress(sourceNode);
	Address* destinationAddress = host_getDefaultAddress(destinationNode);
	return topology_getLatency(slave->topology, sourceAddress, destinationAddress);
}

Configuration* slave_getConfig(Slave* slave) {
	MAGIC_ASSERT(slave);
	return slave->config;
}

SimulationTime slave_getExecuteWindowEnd(Slave* slave) {
	MAGIC_ASSERT(slave);
	return master_getExecuteWindowEnd(slave->master);
}

SimulationTime slave_getEndTime(Slave* slave) {
	MAGIC_ASSERT(slave);
	return master_getEndTime(slave->master);
}

gboolean slave_isKilled(Slave* slave) {
	MAGIC_ASSERT(slave);
	return master_isKilled(slave->master);
}

void slave_setKillTime(Slave* slave, SimulationTime endTime) {
	MAGIC_ASSERT(slave);
	master_setKillTime(slave->master, endTime);
}

void slave_setKilled(Slave* slave, gboolean isKilled) {
	MAGIC_ASSERT(slave);
	master_setKilled(slave->master, isKilled);
}

SimulationTime slave_getMinTimeJump(Slave* slave) {
	MAGIC_ASSERT(slave);
	return master_getMinTimeJump(slave->master);
}

guint slave_getWorkerCount(Slave* slave) {
	MAGIC_ASSERT(slave);
	/* configured number of worker threads, + 1 for main thread */
	return slave->nWorkers + 1;
}

void slave_cryptoLockingFunc(Slave* slave, gint mode, gint n) {
/* from /usr/include/openssl/crypto.h */
#define CRYPTO_LOCK		1
#define CRYPTO_UNLOCK	2
#define CRYPTO_READ		4
#define CRYPTO_WRITE	8

	MAGIC_ASSERT(slave);
	g_assert(slave->cryptoThreadLocks);

	/* TODO may want to replace this with GRWLock when moving to GLib >= 2.32 */
	GMutex* lock = &(slave->cryptoThreadLocks[n]);
	g_assert(lock);

	if(mode & CRYPTO_LOCK) {
		g_mutex_lock(lock);
	} else {
		g_mutex_unlock(lock);
	}
}

gboolean slave_cryptoSetup(Slave* slave, gint numLocks) {
	MAGIC_ASSERT(slave);

	if(numLocks) {
		_slave_lock(slave);

		if(slave->cryptoThreadLocks) {
			g_assert(numLocks <= slave->numCryptoThreadLocks);
		} else {
			slave->numCryptoThreadLocks = numLocks;
			slave->cryptoThreadLocks = g_new0(GMutex, numLocks);
			for(int i = 0; i < slave->numCryptoThreadLocks; i++) {
				g_mutex_init(&(slave->cryptoThreadLocks[i]));
			}
		}

		_slave_unlock(slave);
	}

	return TRUE;
}

void slave_notifyProcessed(Slave* slave, guint numberEventsProcessed, guint numberNodesWithEvents) {
	MAGIC_ASSERT(slave);
	_slave_lock(slave);
	slave->numEventsCurrentInterval += numberEventsProcessed;
	slave->numNodesWithEventsCurrentInterval += numberNodesWithEvents;
	_slave_unlock(slave);
	countdownlatch_countDownAwait(slave->processingLatch);
	countdownlatch_countDownAwait(slave->barrierLatch);
}

void slave_runParallel(Slave* slave) {
	MAGIC_ASSERT(slave);
}

void slave_runSerial(Slave* slave) {
	MAGIC_ASSERT(slave);
	WorkLoad w;
	w.master = slave->master;
	w.slave = slave;
	w.hosts = _slave_getAllHosts(slave);
	worker_runSerial(&w);
}
