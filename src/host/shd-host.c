/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Host {
	/* holds this node's events */
	EventQueue* events;

	/* the network this node belongs to */
	Network* network;

	/* general node lock. nothing that belongs to the node should be touched
	 * unless holding this lock. everything following this falls under the lock.
	 */
	GMutex lock;

	GQuark id;
	gchar* name;
	GHashTable* interfaces;
	NetworkInterface* defaultInterface;
	CPU* cpu;

	/* the applications this node is running */
	GList* applications;

	/* a statistics tracker for in/out bytes, CPU, memory, etc. */
	Tracker* tracker;

	/* this node's loglevel */
	GLogLevelFlags logLevel;

	/* flag on whether or not packets are being captured */
	gchar logPcap;
	
	/* Directory to save PCAP files to if packets are being captured */
	gchar* pcapDir;

	/* all file, socket, and epoll descriptors we know about and track */
	GHashTable* descriptors;
	gint descriptorHandleCounter;
	guint64 receiveBufferSize;
	guint64 sendBufferSize;
	gboolean autotuneReceiveBuffer;
	gboolean autotuneSendBuffer;

	/* track the order in which the application sent us application data */
	gdouble packetPriorityCounter;

	/* random stream */
	Random* random;

	MAGIC_DECLARE;
};

Host* host_new(GQuark id, Network* network, guint32 ip,
		GString* hostname, guint64 bwDownKiBps, guint64 bwUpKiBps,
		guint cpuFrequency, gint cpuThreshold, gint cpuPrecision, guint nodeSeed,
		SimulationTime heartbeatInterval, GLogLevelFlags heartbeatLogLevel, gchar* heartbeatLogInfo,
		GLogLevelFlags logLevel, gboolean logPcap, gchar* pcapDir, gchar* qdisc,
		guint64 receiveBufferSize, gboolean autotuneReceiveBuffer, guint64 sendBufferSize, gboolean autotuneSendBuffer,
		guint64 interfaceReceiveLength) {
	Host* host = g_new0(Host, 1);
	MAGIC_INIT(host);

	host->id = id;
	host->name = g_strdup(hostname->str);
	g_mutex_init(&(host->lock));

	/* thread-level event communication with other nodes */
	host->events = eventqueue_new();

	/* where we are in the network topology */
	host->network = network;

	/* virtual interfaces for managing network I/O */
	host->interfaces = g_hash_table_new_full(g_direct_hash, g_direct_equal,
			NULL, (GDestroyNotify) networkinterface_free);
	NetworkInterface* ethernet = networkinterface_new(network, id, hostname->str, bwDownKiBps, bwUpKiBps, logPcap, pcapDir, qdisc, interfaceReceiveLength);
	g_hash_table_replace(host->interfaces, GUINT_TO_POINTER((guint)id), ethernet);
	GString *loopbackName = g_string_new("");
	g_string_append_printf(loopbackName, "%s-loopback", hostname->str);
	NetworkInterface* loopback = networkinterface_new(NULL, (GQuark)htonl(INADDR_LOOPBACK), loopbackName->str, G_MAXUINT32, G_MAXUINT32, logPcap, pcapDir, qdisc, interfaceReceiveLength);
	g_hash_table_replace(host->interfaces, GUINT_TO_POINTER((guint)htonl(INADDR_LOOPBACK)), loopback);
	host->defaultInterface = ethernet;

	/* virtual descriptor management */
	host->descriptors = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, descriptor_unref);
	host->descriptorHandleCounter = MIN_DESCRIPTOR;
	host->receiveBufferSize = receiveBufferSize;
	host->sendBufferSize = sendBufferSize;
	host->autotuneReceiveBuffer = autotuneReceiveBuffer;
	host->autotuneSendBuffer = autotuneSendBuffer;

<<<<<<< HEAD:src/host/shd-host.c
	/* host order so increments make sense */
	host->randomPortCounter = MIN_RANDOM_PORT;

=======
>>>>>>> master:src/node/shd-node.c
	/* applications this node will run */
//	node->application = application_new(software);

	host->cpu = cpu_new(cpuFrequency, cpuThreshold, cpuPrecision);
	host->random = random_new(nodeSeed);
	host->tracker = tracker_new(heartbeatInterval, heartbeatLogLevel, heartbeatLogInfo);
	host->logLevel = logLevel;
	host->logPcap = logPcap;
	host->pcapDir = pcapDir;

	message("Created Host '%s', ip %s, "
			"%"G_GUINT64_FORMAT" bwUpKiBps, %"G_GUINT64_FORMAT" bwDownKiBps, %"G_GUINT64_FORMAT" initSockSendBufSize, %"G_GUINT64_FORMAT" initSockRecvBufSize, "
			"%u cpuFrequency, %i cpuThreshold, %i cpuPrecision, %u seed",
			g_quark_to_string(host->id), networkinterface_getIPName(host->defaultInterface),
			bwUpKiBps, bwDownKiBps, sendBufferSize, receiveBufferSize,
			cpuFrequency, cpuThreshold, cpuPrecision, nodeSeed);

	return host;
}

void host_free(Host* host, gpointer userData) {
	MAGIC_ASSERT(host);

	g_hash_table_destroy(host->interfaces);
	g_hash_table_destroy(host->descriptors);

	g_free(host->name);

	eventqueue_free(host->events);
	cpu_free(host->cpu);
	tracker_free(host->tracker);

	g_mutex_clear(&(host->lock));

	MAGIC_CLEAR(host);
	g_free(host);
}

void host_lock(Host* host) {
	MAGIC_ASSERT(host);
	g_mutex_lock(&(host->lock));
}

void host_unlock(Host* host) {
	MAGIC_ASSERT(host);
	g_mutex_unlock(&(host->lock));
}

EventQueue* host_getEvents(Host* host) {
	MAGIC_ASSERT(host);
	return host->events;
}

void host_addApplication(Host* host, GQuark pluginID, gchar* pluginPath,
		SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
	MAGIC_ASSERT(host);
	Application* application = application_new(pluginID, pluginPath, startTime, stopTime, arguments);
	host->applications = g_list_append(host->applications, application);

	Worker* worker = worker_getPrivate();
	StartApplicationEvent* event = startapplication_new(application);
	worker_scheduleEvent((Event*)event, startTime, host->id);

	if(stopTime > startTime) {
		StopApplicationEvent* event = stopapplication_new(application);
		worker_scheduleEvent((Event*)event, stopTime, host->id);
	}
}

void host_startApplication(Host* host, Application* application) {
	MAGIC_ASSERT(host);
	application_start(application);
}

void host_stopApplication(Host* host, Application* application) {
	MAGIC_ASSERT(host);
	application_stop(application);
}

void host_freeAllApplications(Host* host, gpointer userData) {
	MAGIC_ASSERT(host);

	Worker* worker = worker_getPrivate();
	worker->cached_node = host;

	GList* item = host->applications;
	while (item && item->data) {
		Application* application = (Application*) item->data;
		application_free(application);
		item->data = NULL;
		item = g_list_next(item);
	}
	g_list_free(host->applications);
	host->applications = NULL;

	worker->cached_node = NULL;
}

gint host_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Host* na = a;
	const Host* nb = b;
	MAGIC_ASSERT(na);
	MAGIC_ASSERT(nb);
	return na->id > nb->id ? +1 : na->id == nb->id ? 0 : -1;
}

gboolean host_isEqual(Host* a, Host* b) {
	if(a == NULL && b == NULL) {
		return TRUE;
	} else if(a == NULL || b == NULL) {
		return FALSE;
	} else {
		return host_compare(a, b, NULL) == 0;
	}
}

CPU* host_getCPU(Host* host) {
	MAGIC_ASSERT(host);
	return host->cpu;
}

Network* host_getNetwork(Host* host) {
	MAGIC_ASSERT(host);
	return host->network;
}

gchar* host_getName(Host* host) {
	MAGIC_ASSERT(host);
	return host->name;
}

in_addr_t host_getDefaultIP(Host* host) {
	MAGIC_ASSERT(host);
	return networkinterface_getIPAddress(host->defaultInterface);
}

gchar* host_getDefaultIPName(Host* host) {
	MAGIC_ASSERT(host);
	return networkinterface_getIPName(host->defaultInterface);
}

Random* host_getRandom(Host* host) {
	MAGIC_ASSERT(host);
	return host->random;
}

gboolean host_autotuneReceiveBuffer(Host* host) {
	MAGIC_ASSERT(host);
	return host->autotuneReceiveBuffer;
}

gboolean host_autotuneSendBuffer(Host* host) {
	MAGIC_ASSERT(host);
	return host->autotuneSendBuffer;
}

Descriptor* host_lookupDescriptor(Host* host, gint handle) {
	MAGIC_ASSERT(host);
	return g_hash_table_lookup(host->descriptors, (gconstpointer) &handle);
}

NetworkInterface* host_lookupInterface(Host* host, in_addr_t handle) {
	MAGIC_ASSERT(host);
	return g_hash_table_lookup(host->interfaces, GUINT_TO_POINTER(handle));
}

static void _host_associateInterface(Host* host, Socket* socket,
		in_addr_t bindAddress, in_port_t bindPort) {
	MAGIC_ASSERT(host);

	/* connect up socket layer */
	socket_setBinding(socket, bindAddress, bindPort);

	/* now associate the interfaces corresponding to bindAddress with socket */
	if(bindAddress == htonl(INADDR_ANY)) {
		/* need to associate all interfaces */
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, host->interfaces);

		while(g_hash_table_iter_next(&iter, &key, &value)) {
			NetworkInterface* interface = value;
			networkinterface_associate(interface, socket);
		}
	} else {
		NetworkInterface* interface = host_lookupInterface(host, bindAddress);
		networkinterface_associate(interface, socket);
	}
}

static void _host_disassociateInterface(Host* host, Socket* socket) {
	in_addr_t bindAddress = socket_getBinding(socket);

	if(bindAddress == htonl(INADDR_ANY)) {
		/* need to dissociate all interfaces */
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, host->interfaces);

		while(g_hash_table_iter_next(&iter, &key, &value)) {
			NetworkInterface* interface = value;
			networkinterface_disassociate(interface, socket);
		}

	} else {
		NetworkInterface* interface = host_lookupInterface(host, bindAddress);
		networkinterface_disassociate(interface, socket);
	}

}

static gint _host_monitorDescriptor(Host* host, Descriptor* descriptor) {
	MAGIC_ASSERT(host);

	/* make sure there are no collisions before inserting */
	gint* handle = descriptor_getHandleReference(descriptor);
	g_assert(handle && !host_lookupDescriptor(host, *handle));
	g_hash_table_replace(host->descriptors, handle, descriptor);

	return *handle;
}

static void _host_unmonitorDescriptor(Host* host, gint handle) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor) {
		if(descriptor->type == DT_TCPSOCKET || descriptor->type == DT_UDPSOCKET)
		{
			Socket* socket = (Socket*) descriptor;
			_host_disassociateInterface(host, socket);
		}

		g_hash_table_remove(host->descriptors, (gconstpointer) &handle);
	}
}

gint host_createDescriptor(Host* host, DescriptorType type) {
	MAGIC_ASSERT(host);

	/* get a unique descriptor that can be "closed" later */
	Descriptor* descriptor;

	switch(type) {
		case DT_EPOLL: {
			descriptor = (Descriptor*) epoll_new((host->descriptorHandleCounter)++);
			break;
		}

		case DT_TCPSOCKET: {
			descriptor = (Descriptor*) tcp_new((host->descriptorHandleCounter)++,
					host->receiveBufferSize, host->sendBufferSize);
			break;
		}

		case DT_UDPSOCKET: {
			descriptor = (Descriptor*) udp_new((host->descriptorHandleCounter)++,
					host->receiveBufferSize, host->sendBufferSize);
			break;
		}

		case DT_SOCKETPAIR: {
			gint handle = (host->descriptorHandleCounter)++;
			gint linkedHandle = (host->descriptorHandleCounter)++;

			/* each channel is readable and writable */
			descriptor = (Descriptor*) channel_new(handle, linkedHandle, CT_NONE);
			Descriptor* linked = (Descriptor*) channel_new(linkedHandle, handle, CT_NONE);
			_host_monitorDescriptor(host, linked);

			break;
		}

		case DT_PIPE: {
			gint handle = (host->descriptorHandleCounter)++;
			gint linkedHandle = (host->descriptorHandleCounter)++;

			/* one side is readonly, the other is writeonly */
			descriptor = (Descriptor*) channel_new(handle, linkedHandle, CT_READONLY);
			Descriptor* linked = (Descriptor*) channel_new(linkedHandle, handle, CT_WRITEONLY);
			_host_monitorDescriptor(host, linked);

			break;
		}

		default: {
			warning("unknown descriptor type: %i", (gint)type);
			return EINVAL;
		}
	}

	return _host_monitorDescriptor(host, descriptor);
}

void host_closeDescriptor(Host* host, gint handle) {
	MAGIC_ASSERT(host);
	_host_unmonitorDescriptor(host, handle);
}

gint host_epollControl(Host* host, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event) {
	MAGIC_ASSERT(host);

	/* EBADF  epfd is not a valid file descriptor. */
	Descriptor* descriptor = host_lookupDescriptor(host, epollDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", epollDescriptor);
		return EBADF;
	}

	/* EINVAL epfd is not an epoll file descriptor */
	if(descriptor_getType(descriptor) != DT_EPOLL) {
		return EINVAL;
	}

	/* now we know its an epoll */
	Epoll* epoll = (Epoll*) descriptor;

	/* if this is for a system file, forward to system call */
	if(fileDescriptor < MIN_DESCRIPTOR) {
		return epoll_controlOS(epoll, operation, fileDescriptor, event);
	}

	/* EBADF  fd is not a valid shadow file descriptor. */
	descriptor = host_lookupDescriptor(host, fileDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", fileDescriptor);
		return EBADF;
	}

	return epoll_control(epoll, operation, descriptor, event);

}

gint host_epollGetEvents(Host* host, gint handle,
		struct epoll_event* eventArray, gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(host);

	/* EBADF  epfd is not a valid file descriptor. */
	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	/* EINVAL epfd is not an epoll file descriptor */
	if(descriptor_getType(descriptor) != DT_EPOLL) {
		return EINVAL;
	}

	Epoll* epoll = (Epoll*) descriptor;
	return epoll_getEvents(epoll, eventArray, eventArrayLength, nEvents);
}

static gboolean _host_doesInterfaceExist(Host* host, in_addr_t interfaceIP) {
	MAGIC_ASSERT(host);

	if(interfaceIP == htonl(INADDR_ANY) && host->defaultInterface) {
		return TRUE;
	}

	NetworkInterface* interface = host_lookupInterface(host, interfaceIP);
	if(interface) {
		return TRUE;
	}

	return FALSE;
}

static gboolean _host_isInterfaceAvailable(Host* host, in_addr_t interfaceIP,
		DescriptorType type, in_port_t port) {
	MAGIC_ASSERT(host);

	enum ProtocolType protocol = type == DT_TCPSOCKET ? PTCP : type == DT_UDPSOCKET ? PUDP : PLOCAL;
	gint associationKey = PROTOCOL_DEMUX_KEY(protocol, port);
	gboolean isAvailable = FALSE;

	if(interfaceIP == htonl(INADDR_ANY)) {
		/* need to check that all interfaces are free */
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, host->interfaces);

		while(g_hash_table_iter_next(&iter, &key, &value)) {
			NetworkInterface* interface = value;
			isAvailable = !networkinterface_isAssociated(interface, associationKey);

			/* as soon as one is taken, break out to return FALSE */
			if(!isAvailable) {
				break;
			}
		}
	} else {
		NetworkInterface* interface = host_lookupInterface(host, interfaceIP);
		isAvailable = !networkinterface_isAssociated(interface, associationKey);
	}

	return isAvailable;
}


static in_port_t _host_getRandomFreePort(Host* host, in_addr_t interfaceIP,
		DescriptorType type) {
	MAGIC_ASSERT(host);

	in_port_t randomNetworkPort = 0;
	gboolean available = FALSE;

	while(!available) {
		gdouble randomFraction = random_nextDouble(host->random);
		in_port_t randomHostPort = (in_port_t) (randomFraction * (UINT16_MAX - MIN_RANDOM_PORT)) + MIN_RANDOM_PORT;
		g_assert(randomHostPort >= MIN_RANDOM_PORT);
		randomNetworkPort = htons(randomHostPort);
		available = _host_isInterfaceAvailable(host, interfaceIP, type, randomNetworkPort);
	}

	return randomNetworkPort;
}

gint host_bindToInterface(Host* host, gint handle, in_addr_t bindAddress, in_port_t bindPort) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return ENOTSOCK;
	}

	/* make sure we have an interface at that address */
	if(!_host_doesInterfaceExist(host, bindAddress)) {
		return EADDRNOTAVAIL;
	}

	Socket* socket = (Socket*) descriptor;

	/* make sure socket is not bound */
	if(socket_getBinding(socket)) {
		warning("socket already bound to requested address");
		return EINVAL;
	}

	/* make sure we have a proper port */
	if(bindPort == 0) {
		/* we know it will be available */
		bindPort = _host_getRandomFreePort(host, bindAddress, type);
	} else {
		/* make sure their port is available at that address for this protocol. */
		if(!_host_isInterfaceAvailable(host, bindAddress, type, bindPort)) {
			return EADDRINUSE;
		}
	}

	/* bind port and set associations */
	_host_associateInterface(host, socket, bindAddress, bindPort);

	return 0;
}

gint host_connectToPeer(Host* host, gint handle, in_addr_t peerAddress,
		in_port_t peerPort, sa_family_t family) {
	MAGIC_ASSERT(host);

	in_addr_t loIP = htonl(INADDR_LOOPBACK);

	/* make sure we will be able to route this later */
	if(peerAddress != loIP) {
		Internetwork* internet = worker_getInternet();
		Network *peerNetwork = internetwork_lookupNetwork(internet, peerAddress);
		if(!peerNetwork) {
			/* can't route it - there is no node with this address */
			gchar* peerAddressString = address_ipToNewString(ntohl(peerAddress));
			warning("attempting to connect to address '%s:%u' for which no host exists", peerAddressString, ntohs(peerPort));
			g_free(peerAddressString);
			return ECONNREFUSED;
		}
	}

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return ENOTSOCK;
	}

	Socket* socket = (Socket*) descriptor;

	if(!socket_isFamilySupported(socket, family)) {
		return EAFNOSUPPORT;
	}

	if(type == DT_TCPSOCKET) {
		gint error = tcp_getConnectError((TCP*)socket);
		if(error) {
			return error;
		}
	}

	if(!socket_getBinding(socket)) {
		/* do an implicit bind to a random port.
		 * use default interface unless the remote peer is on loopback */
		in_addr_t defaultIP = networkinterface_getIPAddress(host->defaultInterface);

		in_addr_t bindAddress = loIP == peerAddress ? loIP : defaultIP;
		in_port_t bindPort = _host_getRandomFreePort(host, bindAddress, type);

		_host_associateInterface(host, socket, bindAddress, bindPort);
	}

	return socket_connectToPeer(socket, peerAddress, peerPort, family);
}

gint host_listenForPeer(Host* host, gint handle, gint backlog) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return EOPNOTSUPP;
	}

	Socket* socket = (Socket*) descriptor;
	TCP* tcp = (TCP*) descriptor;

	if(!socket_isBound(socket)) {
		/* implicit bind */
		in_addr_t bindAddress = htonl(INADDR_ANY);
		in_port_t bindPort = _host_getRandomFreePort(host, bindAddress, type);

		_host_associateInterface(host, socket, bindAddress, bindPort);
	}

	tcp_enterServerMode(tcp, backlog);
	return 0;
}

gint host_acceptNewPeer(Host* host, gint handle, in_addr_t* ip, in_port_t* port, gint* acceptedHandle) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET) {
		return EOPNOTSUPP;
	}

	return tcp_acceptServerPeer((TCP*)descriptor, ip, port, acceptedHandle);
}

gint host_getPeerName(Host* host, gint handle, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET) {
		return ENOTCONN;
	}

	return socket_getPeerName((Socket*)descriptor, ip, port);
}

gint host_getSocketName(Host* host, gint handle, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return ENOTSOCK;
	}

	return socket_getSocketName((Socket*)descriptor, ip, port);
}

gint host_sendUserData(Host* host, gint handle, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_addr_t port, gsize* bytesCopied) {
	MAGIC_ASSERT(host);
	g_assert(bytesCopied);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET && type != DT_PIPE) {
		return EBADF;
	}

	Transport* transport = (Transport*) descriptor;

	/* we should block if our cpu has been too busy lately */
	if(cpu_isBlocked(host->cpu)) {
		debug("blocked on CPU when trying to send %"G_GSIZE_FORMAT" bytes from socket %i", nBytes, handle);

		/*
		 * immediately schedule an event to tell the socket it can write. it will
		 * pop out when the CPU delay is absorbed. otherwise we could miss writes.
		 */
		descriptor_adjustStatus(descriptor, DS_WRITABLE, TRUE);

		return EAGAIN;
	}

	if(type == DT_UDPSOCKET) {
		/* make sure that we have somewhere to send it */
		Socket* socket = (Socket*)transport;
		if(ip == 0 || port == 0) {
			/* its ok as long as they setup a default destination with connect()*/
			if(socket->peerIP == 0 || socket->peerPort == 0) {
				/* we have nowhere to send it */
				return EDESTADDRREQ;
			}
		}

		/* if this socket is not bound, do an implicit bind to a random port */
		if(!socket_getBinding(socket)) {
			in_addr_t bindAddress = ip == htonl(INADDR_LOOPBACK) ? htonl(INADDR_LOOPBACK) :
					networkinterface_getIPAddress(host->defaultInterface);
			in_port_t bindPort = _host_getRandomFreePort(host, bindAddress, type);

			/* bind port and set associations */
			_host_associateInterface(host, socket, bindAddress, bindPort);
		}
	}

	if(type == DT_TCPSOCKET) {
		gint error = tcp_getConnectError((TCP*) transport);
		if(error != EISCONN) {
			if(error == EALREADY) {
				/* we should not be writing if the connection is not ready */
				descriptor_adjustStatus(descriptor, DS_WRITABLE, FALSE);
				return EWOULDBLOCK;
			} else {
				return error;
			}
		}
	}

	gssize n = transport_sendUserData(transport, buffer, nBytes, ip, port);
	if(n > 0) {
		/* user is writing some bytes. */
		*bytesCopied = (gsize)n;
	} else if(n == -2) {
		return ENOTCONN;
	} else if(n < 0) {
		return EWOULDBLOCK;
	}

	return 0;
}

gint host_receiveUserData(Host* host, gint handle, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port, gsize* bytesCopied) {
	MAGIC_ASSERT(host);
	g_assert(ip && port && bytesCopied);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	/* user can still read even if they already called close (DS_CLOSED).
	 * in this case, the descriptor will be unreffed and deleted when it no
	 * longer has data, and the above lookup will fail and return EBADF.
	 */

	DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET && type != DT_PIPE) {
		return EBADF;
	}

	Transport* transport = (Transport*) descriptor;

	/* we should block if our cpu has been too busy lately */
	if(cpu_isBlocked(host->cpu)) {
		debug("blocked on CPU when trying to send %"G_GSIZE_FORMAT" bytes from socket %i", nBytes, handle);

		/*
		 * immediately schedule an event to tell the socket it can read. it will
		 * pop out when the CPU delay is absorbed. otherwise we could miss reads.
		 */
		descriptor_adjustStatus(descriptor, DS_READABLE, TRUE);

		return EAGAIN;
	}

	gssize n = transport_receiveUserData(transport, buffer, nBytes, ip, port);
	if(n > 0) {
		/* user is reading some bytes. */
		*bytesCopied = (gsize)n;
	} else if(n == -2) {
		return ENOTCONN;
	} else if(n < 0) {
		return EWOULDBLOCK;
	}

	return 0;
}

gint host_closeUser(Host* host, gint handle) {
	MAGIC_ASSERT(host);

	Descriptor* descriptor = host_lookupDescriptor(host, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	DescriptorStatus status = descriptor_getStatus(descriptor);
	if(status & DS_CLOSED) {
		warning("descriptor handle '%i' not a valid open descriptor", handle);
		return EBADF;
	}

	descriptor_close(descriptor);

	return 0;
}

Tracker* host_getTracker(Host* host) {
	MAGIC_ASSERT(host);
	return host->tracker;
}

GLogLevelFlags host_getLogLevel(Host* host) {
	MAGIC_ASSERT(host);
	return host->logLevel;
}

gchar host_isLoggingPcap(Host *host) {
	MAGIC_ASSERT(host);
	return host->logPcap;
}

gdouble host_getNextPacketPriority(Host* host) {
	MAGIC_ASSERT(host);
	return ++(host->packetPriorityCounter);
}
