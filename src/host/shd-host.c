/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Host {
    /* holds this node's events */
    EventQueue* events;

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
    GQueue* applications;

    /* a statistics tracker for in/out bytes, CPU, memory, etc. */
    Tracker* tracker;

    /* this node's loglevel */
    GLogLevelFlags logLevel;

    /* flag on whether or not packets are being captured */
    gchar logPcap;

    /* Directory to save PCAP files to if packets are being captured */
    gchar* pcapDir;

    /* virtual descriptor numbers */
    GQueue* availableDescriptors;
    gint descriptorHandleCounter;

    /* virtual process id counter */
    guint processIDCounter;

    /* all file, socket, and epoll descriptors we know about and track */
    GHashTable* descriptors;
    guint64 receiveBufferSize;
    guint64 sendBufferSize;
    gboolean autotuneReceiveBuffer;
    gboolean autotuneSendBuffer;

    /* map from the descriptor handle we returned to the plug-in, and
     * descriptor handle that the OS gave us for files, etc.
     * We do this so that we can give out low descriptor numbers even though the OS
     * may give out those same low numbers when files are opened. */
    GHashTable* shadowToOSHandleMap;
    GHashTable* osToShadowHandleMap;

    /* list of all /dev/random shadow handles that have been created */
    GHashTable* randomShadowHandleMap;

    /* map path to ports for unix sockets */
    GHashTable* unixPathToPortMap;

    /* track the order in which the application sent us application data */
    gdouble packetPriorityCounter;

    /* random stream */
    Random* random;

    gchar* dataDirPath;
    MAGIC_DECLARE;
};

Host* host_new(GQuark id, gchar* hostname, gchar* ipHint, gchar* geocodeHint, gchar* typeHint,
        guint64 requestedBWDownKiBps, guint64 requestedBWUpKiBps,
        guint cpuFrequency, gint cpuThreshold, gint cpuPrecision, guint nodeSeed,
        SimulationTime heartbeatInterval, GLogLevelFlags heartbeatLogLevel, gchar* heartbeatLogInfo,
        GLogLevelFlags logLevel, gboolean logPcap, gchar* pcapDir, gchar* qdisc,
        guint64 receiveBufferSize, gboolean autotuneReceiveBuffer,
        guint64 sendBufferSize, gboolean autotuneSendBuffer,
        guint64 interfaceReceiveLength, const gchar* rootDataPath) {
    Host* host = g_new0(Host, 1);
    MAGIC_INIT(host);

    host->id = id;
    host->name = g_strdup(hostname);
    host->random = random_new(nodeSeed);

    /* get unique virtual address identifiers for each network interface */
    Address* loopbackAddress = dns_register(worker_getDNS(), host->id, host->name, "127.0.0.1");
    Address* ethernetAddress = dns_register(worker_getDNS(), host->id, host->name, ipHint);

    /* connect to topology and get the default bandwidth */
    guint64 bwDownKiBps = 0, bwUpKiBps = 0;
    topology_attach(worker_getTopology(), ethernetAddress, host->random,
            ipHint, geocodeHint, typeHint, &bwDownKiBps, &bwUpKiBps);

    /* prefer assigned bandwidth if available */
    if(requestedBWDownKiBps) {
        bwDownKiBps = requestedBWDownKiBps;
    }
    if(requestedBWUpKiBps) {
        bwUpKiBps = requestedBWUpKiBps;
    }

    /* virtual addresses and interfaces for managing network I/O */
    NetworkInterface* loopback = networkinterface_new(loopbackAddress, G_MAXUINT32, G_MAXUINT32,
            logPcap, pcapDir, qdisc, interfaceReceiveLength);
    NetworkInterface* ethernet = networkinterface_new(ethernetAddress, bwDownKiBps, bwUpKiBps,
            logPcap, pcapDir, qdisc, interfaceReceiveLength);

    host->interfaces = g_hash_table_new_full(g_direct_hash, g_direct_equal,
            NULL, (GDestroyNotify) networkinterface_free);
    g_hash_table_replace(host->interfaces, GUINT_TO_POINTER((guint)networkinterface_getIPAddress(ethernet)), ethernet);
    g_hash_table_replace(host->interfaces, GUINT_TO_POINTER((guint)htonl(INADDR_LOOPBACK)), loopback);

    host->defaultInterface = ethernet;

    /* thread-level event communication with other nodes */
    g_mutex_init(&(host->lock));
    host->events = eventqueue_new();

    host->availableDescriptors = g_queue_new();
    host->descriptorHandleCounter = MIN_DESCRIPTOR;

    /* virtual descriptor management */
    host->descriptors = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, descriptor_unref);
    host->receiveBufferSize = receiveBufferSize;
    host->sendBufferSize = sendBufferSize;
    host->autotuneReceiveBuffer = autotuneReceiveBuffer;
    host->autotuneSendBuffer = autotuneSendBuffer;

    host->shadowToOSHandleMap = g_hash_table_new(g_direct_hash, g_direct_equal);
    host->osToShadowHandleMap = g_hash_table_new(g_direct_hash, g_direct_equal);
    host->randomShadowHandleMap = g_hash_table_new(g_direct_hash, g_direct_equal);
    host->unixPathToPortMap = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* applications this node will run */
    host->applications = g_queue_new();

    host->cpu = cpu_new(cpuFrequency, cpuThreshold, cpuPrecision);
    host->tracker = tracker_new(heartbeatInterval, heartbeatLogLevel, heartbeatLogInfo);
    host->logLevel = logLevel;
    host->logPcap = logPcap;
    host->pcapDir = pcapDir;

    address_unref(loopbackAddress);
    address_unref(ethernetAddress);

    message("Created Host '%s', ip %s, "
            "%"G_GUINT64_FORMAT" bwUpKiBps, %"G_GUINT64_FORMAT" bwDownKiBps, %"G_GUINT64_FORMAT" initSockSendBufSize, %"G_GUINT64_FORMAT" initSockRecvBufSize, "
            "%u cpuFrequency, %i cpuThreshold, %i cpuPrecision, %u seed",
            g_quark_to_string(host->id), networkinterface_getIPName(host->defaultInterface),
            bwUpKiBps, bwDownKiBps, sendBufferSize, receiveBufferSize,
            cpuFrequency, cpuThreshold, cpuPrecision, nodeSeed);

    host->dataDirPath = g_build_filename(rootDataPath, host->name, NULL);
    g_mkdir_with_parents(host->dataDirPath, 0775);
    host->processIDCounter = 1000;
    return host;
}

void host_free(Host* host, gpointer userData) {
    MAGIC_ASSERT(host);

    info("freeing host %s", host->name);

    g_queue_free(host->applications);

    topology_detach(worker_getTopology(), networkinterface_getAddress(host->defaultInterface));

    g_hash_table_destroy(host->interfaces);

    /* tcp servers and their children holds refs to each other. make sure they
     * all get freed by removing the refs in one direction */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, host->descriptors);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        Descriptor* desc = value;
        if(desc && desc->type == DT_TCPSOCKET) {
            tcp_clearAllChildrenIfServer((TCP*)desc);
        }
    }

    g_hash_table_destroy(host->descriptors);
    g_hash_table_destroy(host->shadowToOSHandleMap);
    g_hash_table_destroy(host->osToShadowHandleMap);
    g_hash_table_destroy(host->randomShadowHandleMap);
    g_hash_table_destroy(host->unixPathToPortMap);

    g_free(host->name);

    eventqueue_free(host->events);
    cpu_free(host->cpu);
    tracker_free(host->tracker);

    g_queue_free(host->availableDescriptors);
    random_free(host->random);

    g_mutex_clear(&(host->lock));

    if(host->dataDirPath) {
        g_free(host->dataDirPath);
    }

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

void host_addApplication(Host* host, GQuark pluginID,
        SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
    MAGIC_ASSERT(host);

    guint processID = host->processIDCounter++;
    Process* application = process_new(host, pluginID, processID, startTime, stopTime, arguments);
    g_queue_push_tail(host->applications, application);

    StartApplicationEvent* event = startapplication_new(application);
    worker_scheduleEvent((Event*)event, startTime, host->id);

    if(stopTime > startTime) {
        StopApplicationEvent* event = stopapplication_new(application);
        worker_scheduleEvent((Event*)event, stopTime, host->id);
    }
}

void host_startApplication(Host* host, Process* application) {
    MAGIC_ASSERT(host);
    process_start(application);
}

void host_stopApplication(Host* host, Process* application) {
    MAGIC_ASSERT(host);
    process_stop(application);
}

void host_freeAllApplications(Host* host) {
    MAGIC_ASSERT(host);
    debug("start freeing applications for host '%s'", host->name);
    while(!g_queue_is_empty(host->applications)) {
        process_unref(g_queue_pop_head(host->applications));
    }
    debug("done freeing application for host '%s'", host->name);
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

gchar* host_getName(Host* host) {
    MAGIC_ASSERT(host);
    return host->name;
}

Address* host_getDefaultAddress(Host* host) {
    MAGIC_ASSERT(host);
    return networkinterface_getAddress(host->defaultInterface);
}

// TODO replace this with address object functions
in_addr_t host_getDefaultIP(Host* host) {
    MAGIC_ASSERT(host);
    return networkinterface_getIPAddress(host->defaultInterface);
}

// TODO replace this with address object functions
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
    socket_setSocketName(socket, bindAddress, bindPort, FALSE);

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
    if(!socket || !socket_isBound(socket)) {
        return;
    }

    in_addr_t bindAddress;
    socket_getSocketName(socket, &bindAddress, NULL);

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
    utility_assert(handle && !host_lookupDescriptor(host, *handle));
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

static gint _host_compareDescriptors(gconstpointer a, gconstpointer b, gpointer userData) {
  gint aint = GPOINTER_TO_INT(a);
  gint bint = GPOINTER_TO_INT(b);
  return aint < bint ? -1 : aint == bint ? 0 : 1;
}

static gint _host_getNextDescriptorHandle(Host* host) {
    MAGIC_ASSERT(host);
    if(g_queue_get_length(host->availableDescriptors) > 0) {
        return GPOINTER_TO_INT(g_queue_pop_head(host->availableDescriptors));
    }
    return (host->descriptorHandleCounter)++;
}

static void _host_returnPreviousDescriptorHandle(Host* host, gint handle) {
    MAGIC_ASSERT(host);
    if(handle >= 3) {
        g_queue_insert_sorted(host->availableDescriptors, GINT_TO_POINTER(handle), _host_compareDescriptors, NULL);
    }
}

void host_returnHandleHack(gint handle) {
    /* TODO replace this with something more graceful? */
    Host* host = worker_getCurrentHost();
    if(host) {
        _host_returnPreviousDescriptorHandle(host, handle);
    }
}

gboolean host_isShadowDescriptor(Host* host, gint handle) {
    MAGIC_ASSERT(host);
    return host_lookupDescriptor(host, handle) == NULL ? FALSE : TRUE;
}

gint host_createShadowHandle(Host* host, gint osHandle) {
    MAGIC_ASSERT(host);

    /* stdin, stdout, stderr */
    if(osHandle >=0 && osHandle <= 2) {
        return osHandle;
    }

    /* reserve a new virtual descriptor number to emulate the given osHandle,
     * so that the plugin will not be given duplicate shadow/os numbers. */
    gint shadowHandle = _host_getNextDescriptorHandle(host);

    g_hash_table_replace(host->shadowToOSHandleMap, GINT_TO_POINTER(shadowHandle), GINT_TO_POINTER(osHandle));
    g_hash_table_replace(host->osToShadowHandleMap, GINT_TO_POINTER(osHandle), GINT_TO_POINTER(shadowHandle));

    return shadowHandle;
}

gint host_getShadowHandle(Host* host, gint osHandle) {
    MAGIC_ASSERT(host);

    /* stdin, stdout, stderr */
    if(osHandle >=0 && osHandle <= 2) {
        return osHandle;
    }

    /* find shadow handle that we mapped, if one exists */
    gpointer shadowHandleP = g_hash_table_lookup(host->osToShadowHandleMap, GINT_TO_POINTER(osHandle));

    return shadowHandleP ? GPOINTER_TO_INT(shadowHandleP) : -1;
}

gint host_getOSHandle(Host* host, gint shadowHandle) {
    MAGIC_ASSERT(host);

    /* stdin, stdout, stderr */
    if(shadowHandle >=0 && shadowHandle <= 2) {
        return shadowHandle;
    }

    /* find os handle that we mapped, if one exists */
    gpointer osHandleP = g_hash_table_lookup(host->shadowToOSHandleMap, GINT_TO_POINTER(shadowHandle));

    return osHandleP ? GPOINTER_TO_INT(osHandleP) : -1;
}

void host_setRandomHandle(Host* host, gint handle) {
    MAGIC_ASSERT(host);
    g_hash_table_insert(host->randomShadowHandleMap, GINT_TO_POINTER(handle), GINT_TO_POINTER(handle));
}

gboolean host_isRandomHandle(Host* host, gint handle) {
    MAGIC_ASSERT(host);
    return g_hash_table_contains(host->randomShadowHandleMap, GINT_TO_POINTER(handle));
}


void host_destroyShadowHandle(Host* host, gint shadowHandle) {
    MAGIC_ASSERT(host);

    /* stdin, stdout, stderr */
    if(shadowHandle >=0 && shadowHandle <= 2) {
        return;
    }

    gint osHandle = host_getOSHandle(host, shadowHandle);
    gboolean didExist = g_hash_table_remove(host->shadowToOSHandleMap, GINT_TO_POINTER(shadowHandle));
    if(didExist) {
        g_hash_table_remove(host->osToShadowHandleMap, GINT_TO_POINTER(osHandle));
        _host_returnPreviousDescriptorHandle(host, shadowHandle);
    }

    g_hash_table_remove(host->randomShadowHandleMap, GINT_TO_POINTER(shadowHandle));
}

gint host_createDescriptor(Host* host, DescriptorType type) {
    MAGIC_ASSERT(host);

    /* get a unique descriptor that can be "closed" later */
    Descriptor* descriptor;

    switch(type) {
        case DT_EPOLL: {
            descriptor = (Descriptor*) epoll_new(_host_getNextDescriptorHandle(host));
            break;
        }

        case DT_TCPSOCKET: {
            descriptor = (Descriptor*) tcp_new(_host_getNextDescriptorHandle(host),
                    host->receiveBufferSize, host->sendBufferSize);
            break;
        }

        case DT_UDPSOCKET: {
            descriptor = (Descriptor*) udp_new(_host_getNextDescriptorHandle(host),
                    host->receiveBufferSize, host->sendBufferSize);
            break;
        }

        case DT_SOCKETPAIR: {
            gint handle = _host_getNextDescriptorHandle(host);
            gint linkedHandle = _host_getNextDescriptorHandle(host);

            /* each channel is readable and writable */
            Channel* channel = channel_new(handle, CT_NONE);
            Channel* linked = channel_new(linkedHandle, CT_NONE);
            channel_setLinkedChannel(channel, linked);
            channel_setLinkedChannel(linked, channel);

            _host_monitorDescriptor(host, (Descriptor*)linked);
            descriptor = (Descriptor*) channel;

            break;
        }

        case DT_PIPE: {
            gint handle = _host_getNextDescriptorHandle(host);
            gint linkedHandle = _host_getNextDescriptorHandle(host);

            /* one side is readonly, the other is writeonly */
            Channel* channel = channel_new(handle, CT_READONLY);
            Channel* linked = channel_new(linkedHandle, CT_WRITEONLY);
            channel_setLinkedChannel(channel, linked);
            channel_setLinkedChannel(linked, channel);

            _host_monitorDescriptor(host, (Descriptor*)linked);
            descriptor = (Descriptor*) channel;

            break;
        }

        case DT_TIMER: {
            gint handle = _host_getNextDescriptorHandle(host);
            descriptor = (Descriptor*) timer_new(handle, CLOCK_MONOTONIC, 0);
            break;
        }

        default: {
            warning("unknown descriptor type: %i", (gint)type);
            errno = EINVAL;
            return -1;
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
    if(!host_isShadowDescriptor(host, fileDescriptor)) {
        gint osfd = host_getOSHandle(host, fileDescriptor);
        osfd = osfd >= 0 ? osfd : fileDescriptor;
        return epoll_controlOS(epoll, operation, osfd, event);
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
    gint ret = epoll_getEvents(epoll, eventArray, eventArrayLength, nEvents);

    /* i think data is a user-only struct, and a union - which may not have fd set
     * so lets just leave it alone */
//    for(gint i = 0; i < *nEvents; i++) {
//        if(!host_isShadowDescriptor(host, eventArray[i].data.fd)) {
//            /* the fd is a file that the OS handled for us, translate to shadow fd */
//            gint shadowHandle = host_getShadowHandle(host, eventArray[i].data.fd);
//            utility_assert(shadowHandle >= 0);
//            eventArray[i].data.fd = shadowHandle;
//        }
//    }

    return ret;
}

gint host_select(Host* host, fd_set* readable, fd_set* writeable, fd_set* erroneous) {
    MAGIC_ASSERT(host);

    /* if they dont want readability or writeability, then we have nothing to do */
    if(readable == NULL && writeable == NULL) {
        if(erroneous != NULL) {
            FD_ZERO(erroneous);
        }
        return 0;
    }

    GQueue* readyDescsRead = g_queue_new();
    GQueue* readyDescsWrite = g_queue_new();

    /* first look at shadow internal descriptors */
    GList* descs = g_hash_table_get_values(host->descriptors);
    GList* item = g_list_first(descs);

    /* iterate all descriptors */
    while(item) {
        Descriptor* desc = item->data;
        if(desc) {
            DescriptorStatus status = descriptor_getStatus(desc);
            if((readable != NULL) && FD_ISSET(desc->handle, readable) && (status & DS_ACTIVE) && (status & DS_READABLE)) {
                g_queue_push_head(readyDescsRead, GINT_TO_POINTER(desc->handle));
            }
            if((writeable != NULL) && FD_ISSET(desc->handle, writeable) && (status & DS_ACTIVE) && (status & DS_WRITABLE)) {
                g_queue_push_head(readyDescsWrite, GINT_TO_POINTER(desc->handle));
            }
        }
        item = g_list_next(item);
    }
    /* cleanup the iterator lists */
    g_list_free(descs);
    item = descs = NULL;

    /* now check on OS descriptors */
    struct timeval zeroTimeout;
    zeroTimeout.tv_sec = 0;
    zeroTimeout.tv_usec = 0;
    fd_set osFDSet;

    /* setup our iterator */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, host->shadowToOSHandleMap);

    /* iterate all os handles and ask the os for events */
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        gint shadowHandle = GPOINTER_TO_INT(key);
        gint osHandle = GPOINTER_TO_INT(value);

        if ((readable != NULL) && FD_ISSET(shadowHandle, readable)) {
            FD_ZERO(&osFDSet);
            FD_SET(osHandle, &osFDSet);
            select(osHandle+1, &osFDSet, NULL, NULL, &zeroTimeout);
            if (FD_ISSET(osHandle, &osFDSet)) {
                g_queue_push_head(readyDescsRead, GINT_TO_POINTER(shadowHandle));
            }
        }
        if ((writeable != NULL) && FD_ISSET(shadowHandle, writeable)) {
            FD_ZERO(&osFDSet);
            FD_SET(osHandle, &osFDSet);
            select(osHandle+1, NULL, &osFDSet, NULL, &zeroTimeout);
            if (FD_ISSET(osHandle, &osFDSet)) {
                g_queue_push_head(readyDescsWrite, GINT_TO_POINTER(shadowHandle));
            }
        }
    }

    /* now prepare and return the response, start with empty sets */
    if(readable != NULL) {
        FD_ZERO(readable);
    }
    if(writeable != NULL) {
        FD_ZERO(writeable);
    }
    if(erroneous != NULL) {
        FD_ZERO(erroneous);
    }
    gint nReady = 0;

    /* mark all of the readable handles */
    if(readable != NULL) {
        while(!g_queue_is_empty(readyDescsRead)) {
            gint handle = GPOINTER_TO_INT(g_queue_pop_head(readyDescsRead));
            FD_SET(handle, readable);
            nReady++;
        }
    }
    /* cleanup */
    g_queue_free(readyDescsRead);

    /* mark all of the writeable handles */
    if(writeable != NULL) {
        while(!g_queue_is_empty(readyDescsWrite)) {
            gint handle = GPOINTER_TO_INT(g_queue_pop_head(readyDescsWrite));
            FD_SET(handle, writeable);
            nReady++;
        }
    }
    /* cleanup */
    g_queue_free(readyDescsWrite);

    /* return the total number of bits that are set in all three fdsets */
    return nReady;
}

gint host_poll(Host* host, struct pollfd *pollFDs, nfds_t numPollFDs) {
    MAGIC_ASSERT(host);

    gint numReady = 0;

    for(nfds_t i = 0; i < numPollFDs; i++) {
        struct pollfd* pfd = &pollFDs[i];
        pfd->revents = 0;

        if(pfd->fd == -1) {
            continue;
        }

        if(host_isShadowDescriptor(host, pfd->fd)){
            /* descriptor lookup is not NULL */
            Descriptor* descriptor = host_lookupDescriptor(host, pfd->fd);
            DescriptorStatus status = descriptor_getStatus(descriptor);
            if(status & DS_CLOSED) {
                pfd->revents |= POLLNVAL;
            }

            if(pfd->events != 0) {
                if((pfd->events & POLLIN) && (status & DS_ACTIVE) && (status & DS_READABLE)) {
                    pfd->revents |= POLLIN;
                }
                if((pfd->events & POLLOUT) && (status & DS_ACTIVE) && (status & DS_WRITABLE)) {
                    pfd->revents |= POLLOUT;
                }
            }
        } else {
            /* check if we have a mapped os fd */
            gint osfd = host_getOSHandle(host, pfd->fd);
            if(osfd >= 0) {
                /* ask the OS, but dont let them block */
                gint oldfd = pfd->fd;
                pfd->fd = osfd;
                gint rc = poll(pfd, (nfds_t)1, 0);
                pfd->fd = oldfd;
                if(rc < 0) {
                    return -1;
                }
            }
        }

        numReady += (pfd->revents == 0) ? 0 : 1;
    }

    return numReady;
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

static in_port_t _host_getRandomPort(Host* host) {
    gdouble randomFraction = random_nextDouble(host->random);
    in_port_t randomHostPort = (in_port_t) (randomFraction * (UINT16_MAX - MIN_RANDOM_PORT)) + MIN_RANDOM_PORT;
    utility_assert(randomHostPort >= MIN_RANDOM_PORT);
    return htons(randomHostPort);
}

static in_port_t _host_getRandomFreePort(Host* host, in_addr_t interfaceIP, DescriptorType type) {
    MAGIC_ASSERT(host);

    /* we need a random port that is free everywhere we need it to be.
     * we have two modes here: first we just try grabbing a random port until we
     * get a free one. if we cannot find one in an expected number of loops
     * (based on how many we think are free), then we do an inefficient linear
     * search that is guaranteed to succeed/fail as a fallback. */

    /* lets see if we have enough free ports to just choose randomly */
    guint maxNumBound = 0;

    if(interfaceIP == htonl(INADDR_ANY)) {
        /* need to make sure the port is free on all interfaces */
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, host->interfaces);

        while(g_hash_table_iter_next(&iter, &key, &value)) {
            NetworkInterface* interface = value;
            if(interface) {
                guint numBoundSockets = networkinterface_getAssociationCount(interface);
                maxNumBound = MAX(numBoundSockets, maxNumBound);
            }
        }
    } else {
        /* just check the one at the given IP */
        NetworkInterface* interface = host_lookupInterface(host, interfaceIP);
        if(interface) {
            guint numBoundSockets = networkinterface_getAssociationCount(interface);
            maxNumBound = MAX(numBoundSockets, maxNumBound);
        }
    }

    guint numAllocatablePorts = (guint)(UINT16_MAX - MIN_RANDOM_PORT);
    guint numFreePorts = 0;
    if(maxNumBound < numAllocatablePorts) {
        numFreePorts = numAllocatablePorts - maxNumBound;
    }

    /* we will try to get a port */
    in_port_t randomNetworkPort = 0;

    /* if more than 1/10 of allocatable ports are free, choose randomly but only
     * until we try to many times */
    guint threshold = (guint)(numAllocatablePorts / 100);
    if(numFreePorts >= threshold) {
        guint numTries = 0;
        while(numTries < numFreePorts) {
            in_port_t randomPort = _host_getRandomPort(host);

            /* this will check all interfaces in the case of INADDR_ANY */
            if(_host_isInterfaceAvailable(host, interfaceIP, type, randomPort)) {
                randomNetworkPort = randomPort;
                break;
            }

            numTries++;
        }
    }

    /* now if we tried too many times and still don't have a port, fall back
     * to a linear search to make sure we get a free port if we have one */
    if(!randomNetworkPort) {
        for(in_port_t i = MIN_RANDOM_PORT; i < UINT16_MAX; i++) {
            /* this will check all interfaces in the case of INADDR_ANY */
            if(_host_isInterfaceAvailable(host, interfaceIP, type, i)) {
                randomNetworkPort = i;
                break;
            }
        }
    }

    /* this will return 0 if we can't find a free port */
    return randomNetworkPort;
}

gint host_bindToInterface(Host* host, gint handle, const struct sockaddr* address) {
    MAGIC_ASSERT(host);

    in_addr_t bindAddress = 0;
    in_port_t bindPort = 0;

    if(address->sa_family == AF_INET) {
        struct sockaddr_in* saddr = (struct sockaddr_in*) address;
        bindAddress = saddr->sin_addr.s_addr;
        bindPort = saddr->sin_port;
    } else if (address->sa_family == AF_UNIX) {
        struct sockaddr_un* saddr = (struct sockaddr_un*) address;
        /* cant bind twice to the same unix path */
        if(g_hash_table_lookup(host->unixPathToPortMap, saddr->sun_path)) {
            return EADDRINUSE;
        }
        bindAddress = htonl(INADDR_LOOPBACK);
        bindPort = 0; /* choose a random free port below */
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

    /* make sure we have an interface at that address */
    if(!_host_doesInterfaceExist(host, bindAddress)) {
        return EADDRNOTAVAIL;
    }

    Socket* socket = (Socket*) descriptor;

    /* make sure socket is not bound */
    if(socket_isBound(socket)) {
        warning("socket already bound to requested address");
        return EINVAL;
    }

    /* make sure we have a proper port */
    if(bindPort == 0) {
        /* we know it will be available */
        bindPort = _host_getRandomFreePort(host, bindAddress, type);
        if(!bindPort) {
            return EADDRNOTAVAIL;
        }
    } else {
        /* make sure their port is available at that address for this protocol. */
        if(!_host_isInterfaceAvailable(host, bindAddress, type, bindPort)) {
            return EADDRINUSE;
        }
    }

    /* bind port and set associations */
    _host_associateInterface(host, socket, bindAddress, bindPort);

    if (address->sa_family == AF_UNIX) {
        struct sockaddr_un* saddr = (struct sockaddr_un*) address;
        gchar* sockpath = g_strndup(saddr->sun_path, 108); /* UNIX_PATH_MAX=108 */
        socket_setUnixPath(socket, sockpath, TRUE);
        g_hash_table_replace(host->unixPathToPortMap, sockpath, GUINT_TO_POINTER(bindPort));
    }

    return 0;
}

gint host_connectToPeer(Host* host, gint handle, const struct sockaddr* address) {
    MAGIC_ASSERT(host);

    sa_family_t family = 0;
    in_addr_t peerIP = 0;
    in_port_t peerPort = 0;

    if(address->sa_family == AF_INET) {
        struct sockaddr_in* saddr = (struct sockaddr_in*) address;

        family = saddr->sin_family;
        peerIP = saddr->sin_addr.s_addr;
        peerPort = saddr->sin_port;

        if(peerIP == htonl(INADDR_ANY)) {
            peerIP = htonl(INADDR_LOOPBACK);
        }
    } else if (address->sa_family == AF_UNIX) {
        struct sockaddr_un* saddr = (struct sockaddr_un*) address;

        family = saddr->sun_family;
        gchar* sockpath = saddr->sun_path;

        peerIP = htonl(INADDR_LOOPBACK);
        gpointer val = g_hash_table_lookup(host->unixPathToPortMap, sockpath);
        if(val) {
            peerPort = (in_port_t)GPOINTER_TO_UINT(val);
        }
    }

    in_addr_t loIP = htonl(INADDR_LOOPBACK);

    /* make sure we will be able to route this later */
    if(peerIP != loIP) {
        Address* myAddress = networkinterface_getAddress(host->defaultInterface);
        Address* peerAddress = dns_resolveIPToAddress(worker_getDNS(), peerIP);
        if(!peerAddress || !topology_isRoutable(worker_getTopology(), myAddress, peerAddress)) {
            /* can't route it - there is no node with this address */
            gchar* peerAddressString = address_ipToNewString(peerIP);
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

    if (address->sa_family == AF_UNIX) {
        struct sockaddr_un* saddr = (struct sockaddr_un*) address;
        socket_setUnixPath(socket, saddr->sun_path, FALSE);
    }

    if(!socket_isBound(socket)) {
        /* do an implicit bind to a random port.
         * use default interface unless the remote peer is on loopback */
        in_addr_t defaultIP = networkinterface_getIPAddress(host->defaultInterface);

        in_addr_t bindAddress = loIP == peerIP ? loIP : defaultIP;
        in_port_t bindPort = _host_getRandomFreePort(host, bindAddress, type);
        if(!bindPort) {
            return EADDRNOTAVAIL;
        }

        _host_associateInterface(host, socket, bindAddress, bindPort);
    }

    return socket_connectToPeer(socket, peerIP, peerPort, family);
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
        if(!bindPort) {
            return EADDRNOTAVAIL;
        }

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

gint host_getPeerName(Host* host, gint handle, const struct sockaddr* address, socklen_t* len) {
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

    Socket* sock = (Socket*)descriptor;
    in_addr_t ip = 0;
    in_port_t port = 0;

    gboolean hasPeer = socket_getPeerName(sock, &ip, &port);
    if(hasPeer) {
        if(socket_isUnix(sock)) {
            struct sockaddr_un* saddr = (struct sockaddr_un*) address;
            saddr->sun_family = AF_UNIX;
            gchar* unixPath = socket_getUnixPath(sock);
            if(unixPath) {
                g_snprintf(saddr->sun_path, 107, "%s\\0", unixPath);
                *len = offsetof(struct sockaddr_un, sun_path) + strlen(saddr->sun_path) + 1;
            } else {
                *len = sizeof(sa_family_t);
            }
        } else {
            struct sockaddr_in* saddr = (struct sockaddr_in*) address;
            saddr->sin_family = AF_INET;
            saddr->sin_addr.s_addr = ip;
            saddr->sin_port = port;
        }
        return 0;
    } else {
        return ENOTCONN;
    }
}

gint host_getSocketName(Host* host, gint handle, const struct sockaddr* address, socklen_t* len) {
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

    Socket* sock = (Socket*)descriptor;
    in_addr_t ip = 0;
    in_port_t port = 0;

    gboolean isBound = socket_getSocketName((Socket*)descriptor, &ip, &port);

    if(isBound) {
        if(socket_isUnix(sock)) {
            struct sockaddr_un* saddr = (struct sockaddr_un*) address;
            saddr->sun_family = AF_UNIX;
            gchar* unixPath = socket_getUnixPath(sock);
            if(unixPath) {
                g_snprintf(saddr->sun_path, 107, "%s\\0", unixPath);
                *len = offsetof(struct sockaddr_un, sun_path) + strlen(saddr->sun_path) + 1;
            } else {
                *len = sizeof(sa_family_t);
            }
        } else {
            struct sockaddr_in* saddr = (struct sockaddr_in*) address;
            saddr->sin_family = AF_INET;
            saddr->sin_port = port;

            if(ip == htonl(INADDR_ANY)) {
                in_addr_t peerIP = 0;
                if(socket_getPeerName(sock, &peerIP, NULL) && peerIP != htonl(INADDR_LOOPBACK)) {
                    Address* address = networkinterface_getAddress(host->defaultInterface);
                    ip = (in_addr_t) address_toNetworkIP(address);
                }
            }

            saddr->sin_addr.s_addr = ip;
        }
        return 0;
    } else {
        return ENOTCONN;
    }
}

gint host_sendUserData(Host* host, gint handle, gconstpointer buffer, gsize nBytes,
        in_addr_t ip, in_addr_t port, gsize* bytesCopied) {
    MAGIC_ASSERT(host);
    utility_assert(bytesCopied);

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
        if(!socket_isBound(socket)) {
            in_addr_t bindAddress = ip == htonl(INADDR_LOOPBACK) ? htonl(INADDR_LOOPBACK) :
                    networkinterface_getIPAddress(host->defaultInterface);
            in_port_t bindPort = _host_getRandomFreePort(host, bindAddress, type);
            if(!bindPort) {
                return EADDRNOTAVAIL;
            }

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
    utility_assert(ip && port && bytesCopied);

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

const gchar* host_getDataPath(Host* host) {
    MAGIC_ASSERT(host);
    return host->dataDirPath;
}
