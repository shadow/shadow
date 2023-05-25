/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_SOCKET_H_
#define SHD_SOCKET_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <linux/in.h>
#include <linux/socket.h>
#include <linux/un.h>

typedef struct _LegacySocket LegacySocket;
typedef struct _SocketFunctionTable SocketFunctionTable;

#include "main/bindings/c/bindings-opaque.h"
#include "main/core/support/definitions.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/protocol.h"
#include "main/host/syscall_types.h"
#include "main/routing/packet.minimal.h"

typedef bool (*SocketIsFamilySupportedFunc)(LegacySocket* socket, sa_family_t family);
typedef int (*SocketConnectToPeerFunc)(LegacySocket* socket, const Host* host, in_addr_t ip,
                                        in_port_t port, sa_family_t family);
typedef void (*SocketProcessFunc)(LegacySocket* socket, const Host* host, Packet* packet);
typedef void (*SocketDropFunc)(LegacySocket* socket, const Host* host, Packet* packet);

typedef ssize_t (*SocketSendFunc)(LegacySocket* socket, const Thread* thread,
                                 UntypedForeignPtr buffer, gsize nBytes, in_addr_t ip,
                                 in_port_t port);
typedef ssize_t (*SocketReceiveFunc)(LegacySocket* socket, const Thread* thread,
                                    UntypedForeignPtr buffer, gsize nBytes, in_addr_t* ip,
                                    in_port_t* port);

struct _SocketFunctionTable {
    LegacyFileCloseFunc close;
    LegacyFileCleanupFunc cleanup;
    LegacyFileFreeFunc free;
    SocketSendFunc send;
    SocketReceiveFunc receive;
    SocketProcessFunc process;
    SocketIsFamilySupportedFunc isFamilySupported;
    SocketConnectToPeerFunc connectToPeer;
    SocketDropFunc dropPacket;
    MAGIC_DECLARE_ALWAYS;
};

enum SocketFlags {
    SF_NONE = 0,
    SF_BOUND = 1 << 0,
    SF_UNIX = 1 << 1,
    SF_UNIX_BOUND = 1 << 2,
};

struct _LegacySocket {
    LegacyFile super;
    SocketFunctionTable* vtable;

    enum SocketFlags flags;
    ProtocolType protocol;

    in_addr_t peerIP;
    in_addr_t peerPort;
    gchar* peerString;

    in_addr_t boundAddress;
    in_port_t boundPort;
    gchar* boundString;

    gchar* unixPath;

    /* buffering packets readable by user */
    GQueue* inputBuffer;
    gsize inputBufferSize;
    gsize inputBufferSizePending;
    gsize inputBufferLength;

    /* buffering packets ready to send */
    GQueue* outputBuffer;
    GQueue* outputControlBuffer;
    gsize outputBufferSize;
    gsize outputBufferSizePending;
    gsize outputBufferLength;

    MAGIC_DECLARE_ALWAYS;
};

void legacysocket_init(LegacySocket* socket, const Host* host, SocketFunctionTable* vtable,
                       LegacyFileType type, guint receiveBufferSize, guint sendBufferSize);

void legacysocket_pushInPacket(LegacySocket* socket, const Host* host, Packet* packet);
void legacysocket_dropPacket(LegacySocket* socket, const Host* host, Packet* packet);
Packet* legacysocket_pullOutPacket(LegacySocket* socket, const Host* host);
Packet* legacysocket_peekNextOutPacket(const LegacySocket* socket);
Packet* legacysocket_peekNextInPacket(const LegacySocket* socket);

ssize_t legacysocket_sendUserData(LegacySocket* socket, const Thread* thread,
                                 UntypedForeignPtr buffer, gsize nBytes, in_addr_t ip,
                                 in_port_t port);
ssize_t legacysocket_receiveUserData(LegacySocket* socket, const Thread* thread,
                                    UntypedForeignPtr buffer, gsize nBytes, in_addr_t* ip,
                                    in_port_t* port);

size_t legacysocket_getInputBufferSize(LegacySocket* socket);
void legacysocket_setInputBufferSize(LegacySocket* socket, gsize newSize);
size_t legacysocket_getInputBufferLength(LegacySocket* socket);
size_t legacysocket_getInputBufferSpace(LegacySocket* socket);
bool legacysocket_addToInputBuffer(LegacySocket* socket, const Host* host, Packet* packet);
Packet* legacysocket_removeFromInputBuffer(LegacySocket* socket, const Host* host);

size_t legacysocket_getOutputBufferSize(LegacySocket* socket);
void legacysocket_setOutputBufferSize(LegacySocket* socket, gsize newSize);
size_t legacysocket_getOutputBufferLength(LegacySocket* socket);
size_t legacysocket_getOutputBufferSpace(LegacySocket* socket);
bool legacysocket_addToOutputBuffer(LegacySocket* socket, const Host* host, Packet* packet);
Packet* legacysocket_removeFromOutputBuffer(LegacySocket* socket, const Host* host);

bool legacysocket_isBound(LegacySocket* socket);
bool legacysocket_getPeerName(LegacySocket* socket, in_addr_t* ip, in_port_t* port);
void legacysocket_setPeerName(LegacySocket* socket, in_addr_t ip, in_port_t port);
bool legacysocket_getSocketName(LegacySocket* socket, in_addr_t* ip, in_port_t* port);
void legacysocket_setSocketName(LegacySocket* socket, in_addr_t ip, in_port_t port);

ProtocolType legacysocket_getProtocol(LegacySocket* socket);

bool legacysocket_isFamilySupported(LegacySocket* socket, sa_family_t family);
int legacysocket_connectToPeer(LegacySocket* socket, const Host* host, in_addr_t ip,
                                in_port_t port, sa_family_t family);

bool legacysocket_isUnix(LegacySocket* socket);
void legacysocket_setUnix(LegacySocket* socket, gboolean isUnixSocket);
void legacysocket_setUnixPath(LegacySocket* socket, const gchar* path, gboolean isBound);
char* legacysocket_getUnixPath(LegacySocket* socket);

#endif /* SHD_SOCKET_H_ */
