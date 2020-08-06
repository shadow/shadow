//
// Created by ilios on 20. 8. 3..
//
#include "shadow.h"
#include <zmq.h>

static IPC_Conf shadow_ipc_conf;

void init_ipc() {
    shadow_ipc_conf.zmq_context = zmq_ctx_new();
    // create an ZMQ socket (for type 'publisher')
    shadow_ipc_conf.zmq_data_socket = zmq_socket(shadow_ipc_conf.zmq_context, ZMQ_PUB);

    // connect to ZMQ server
    const int rb = zmq_connect(shadow_ipc_conf.zmq_data_socket, "tcp://127.0.0.1:5555");

    if (rb == 0) {
        shadow_ipc_conf.initialized = 1;
    } else {
        shadow_ipc_conf.initialized = 0;
    }
}

gboolean is_ipc_initialized() {
    return shadow_ipc_conf.initialized;
}

void sendIPC_tcp_connect(int fd, const struct sockaddr* addr, socklen_t len) {
    if (addr->sa_family != AF_INET) {
        // not an IPv4 address
        return;
    }

    // get interested values for 'connect'
    struct sockaddr_in* inaddr = (struct sockaddr_in*)addr;
    uint16_t port = inaddr->sin_port;
    uint32_t in_addr = (uint32_t)(inaddr->sin_addr.s_addr);
    const char *TOPIC = "shadow_tcp_control";
    const size_t topic_size = strlen(TOPIC);
    uint32_t from_addr;
    Host* activeHost = worker_getActiveHost();
    if(activeHost) {
        Address* hostAddress = host_getDefaultAddress(activeHost);
        if(hostAddress) {
            from_addr = address_toNetworkIP(hostAddress);
        }
    }

    // get current virtual time
    uint64_t curTime = worker_getCurrentTime();

    // create an envelope
    zmq_msg_t envelope;
    const size_t envelope_size = topic_size + 1 + sizeof(uint64_t) + sizeof(int) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint32_t);

    const int rmi = zmq_msg_init_size(&envelope, envelope_size);
    if (rmi != 0)
    {
        printf("ERROR: ZeroMQ error occurred during zmq_msg_init_size(): %s\n", zmq_strerror(errno));
        zmq_msg_close(&envelope);
        return;
    }
    memcpy(zmq_msg_data(&envelope), TOPIC, topic_size);
    void *offset = (void*)((char*)zmq_msg_data(&envelope) + topic_size);
    memcpy(offset, " ", 1);
    offset = (void*) ((char*)offset + 1);
    memcpy(offset, &curTime, sizeof(uint64_t));
    offset = (void*) ((char*)offset + sizeof(uint64_t));
    memcpy(offset, &fd, sizeof(int));
    offset = (void*) ((char*)offset + sizeof(int));
    memcpy(offset, &from_addr, sizeof(uint32_t));
    offset = (void*) ((char*)offset + sizeof(uint32_t));
    memcpy(offset, &port, sizeof(uint16_t));
    offset = (void*) ((char*)offset + sizeof(uint16_t));
    memcpy(offset, &in_addr, sizeof(uint32_t));

//    memcpy(zmq_msg_data(&envelope), TOPIC, topic_size);
//    memcpy((void*)((char*)zmq_msg_data(&envelope) + topic_size), " ", 1);
//    memcpy((void*)((char*)zmq_msg_data(&envelope) + topic_size + 1), &fd, sizeof(int));
//    memcpy((void*)((char*)zmq_msg_data(&envelope) + topic_size + 1 + sizeof(int)), &port, sizeof(uint16_t));
//    memcpy((void*)((char*)zmq_msg_data(&envelope) + topic_size + 1 + sizeof(int) + sizeof(uint16_t)), &in_addr, sizeof(uint32_t));

    // send an envelope through zmq
    const size_t rs = zmq_msg_send(&envelope, shadow_ipc_conf.zmq_data_socket, 0);
    if (rs != envelope_size) {
        printf("ERROR: ZeroMQ error occurred during zmq_msg_send(): %s\n", zmq_strerror(errno));
        zmq_msg_close(&envelope);
        return;
    }
    zmq_msg_close(&envelope);
}

void sendIPC_tcp_send(Socket* socket, int fd, const void *buf, size_t n, int flags) {
    // get interested values for 'connect'
    const char *TOPIC = "shadow_tcp_datastream";
    const size_t topic_size = strlen(TOPIC);
    uint16_t from_port;
    uint32_t from_addr;
    uint16_t peer_port;
    uint32_t peer_addr;
    from_port = (uint16_t)socket->boundPort;
    from_addr = (uint32_t)socket->boundAddress;
    if (from_addr == 0) {
        Host* activeHost = worker_getActiveHost();
        if(activeHost) {
            Address* hostAddress = host_getDefaultAddress(activeHost);
            if(hostAddress) {
                from_addr = address_toNetworkIP(hostAddress);
            }
        }
    }
    peer_port = (uint16_t)socket->peerPort;
    peer_addr = (uint32_t)socket->peerIP;

    // get current virtual time
    uint64_t curTime = worker_getCurrentTime();

    // create an envelope
    zmq_msg_t envelope;
    const size_t envelope_size = topic_size + 1 + sizeof(uint64_t) + sizeof(int) + n + sizeof(uint16_t)*2 + sizeof(uint32_t)*2;

    const int rmi = zmq_msg_init_size(&envelope, envelope_size);
    if (rmi != 0)
    {
        printf("ERROR: ZeroMQ error occurred during zmq_msg_init_size(): %s\n", zmq_strerror(errno));
        zmq_msg_close(&envelope);
        return;
    }

    memcpy(zmq_msg_data(&envelope), TOPIC, topic_size);
    void *offset = (void*)((char*)zmq_msg_data(&envelope) + topic_size);
    memcpy(offset, " ", 1);
    offset = (void*) ((char*)offset + 1);
    memcpy(offset, &curTime, sizeof(uint64_t));
    offset = (void*) ((char*)offset + sizeof(uint64_t));
    memcpy(offset, &fd, sizeof(int));
    offset = (void*) ((char*)offset + sizeof(int));
    memcpy(offset, &from_port, sizeof(uint16_t));
    offset = (void*) ((char*)offset + sizeof(uint16_t));
    memcpy(offset, &from_addr, sizeof(uint32_t));
    offset = (void*) ((char*)offset + sizeof(uint32_t));
    memcpy(offset, &peer_port, sizeof(uint16_t));
    offset = (void*) ((char*)offset + sizeof(uint16_t));
    memcpy(offset, &peer_addr, sizeof(uint32_t));
    offset = (void*) ((char*)offset + sizeof(uint32_t));
    memcpy(offset, buf, n);



    // send an envelope through zmq
    const size_t rs = zmq_msg_send(&envelope, shadow_ipc_conf.zmq_data_socket, 0);
    if (rs != envelope_size) {
        printf("ERROR: ZeroMQ error occurred during zmq_msg_send(): %s\n", zmq_strerror(errno));
        zmq_msg_close(&envelope);
        return;
    }
    zmq_msg_close(&envelope);
}
