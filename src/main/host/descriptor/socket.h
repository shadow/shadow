/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_SOCKET_H_
#define SHD_SOCKET_H_

#include <glib.h>
#include <netinet/in.h>
#include <sys/un.h>

typedef struct _LegacySocket LegacySocket;
typedef struct _SocketFunctionTable SocketFunctionTable;

#include "main/bindings/c/bindings-opaque.h"
#include "main/core/definitions.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/protocol.h"
#include "main/routing/packet.minimal.h"

typedef gboolean (*SocketIsFamilySupportedFunc)(LegacySocket* socket, sa_family_t family);
typedef gint (*SocketConnectToPeerFunc)(LegacySocket* socket, const Host* host, in_addr_t ip,
                                        in_port_t port, sa_family_t family);
typedef void (*SocketProcessFunc)(LegacySocket* socket, const Host* host, Packet* packet);
typedef void (*SocketDropFunc)(LegacySocket* socket, const Host* host, Packet* packet);

typedef gssize (*SocketSendFunc)(LegacySocket* socket, const Thread* thread,
                                 UntypedForeignPtr buffer, gsize nBytes, in_addr_t ip,
                                 in_port_t port);
typedef gssize (*SocketReceiveFunc)(LegacySocket* socket, const Thread* thread,
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

gssize legacysocket_sendUserData(LegacySocket* socket, const Thread* thread,
                                 UntypedForeignPtr buffer, gsize nBytes, in_addr_t ip,
                                 in_port_t port);
gssize legacysocket_receiveUserData(LegacySocket* socket, const Thread* thread,
                                    UntypedForeignPtr buffer, gsize nBytes, in_addr_t* ip,
                                    in_port_t* port);

gsize legacysocket_getInputBufferSize(LegacySocket* socket);
void legacysocket_setInputBufferSize(LegacySocket* socket, gsize newSize);
gsize legacysocket_getInputBufferLength(LegacySocket* socket);
gsize legacysocket_getInputBufferSpace(LegacySocket* socket);
gboolean legacysocket_addToInputBuffer(LegacySocket* socket, const Host* host, Packet* packet);
Packet* legacysocket_removeFromInputBuffer(LegacySocket* socket, const Host* host);

gsize legacysocket_getOutputBufferSize(LegacySocket* socket);
void legacysocket_setOutputBufferSize(LegacySocket* socket, gsize newSize);
gsize legacysocket_getOutputBufferLength(LegacySocket* socket);
gsize legacysocket_getOutputBufferSpace(LegacySocket* socket);
gboolean legacysocket_addToOutputBuffer(LegacySocket* socket, InetSocket* inetSocket,
                                        const Host* host, Packet* packet);
Packet* legacysocket_removeFromOutputBuffer(LegacySocket* socket, const Host* host);

gboolean legacysocket_isBound(LegacySocket* socket);
gboolean legacysocket_getPeerName(LegacySocket* socket, in_addr_t* ip, in_port_t* port);
void legacysocket_setPeerName(LegacySocket* socket, in_addr_t ip, in_port_t port);
gboolean legacysocket_getSocketName(LegacySocket* socket, in_addr_t* ip, in_port_t* port);
void legacysocket_setSocketName(LegacySocket* socket, in_addr_t ip, in_port_t port);

ProtocolType legacysocket_getProtocol(LegacySocket* socket);

gboolean legacysocket_isFamilySupported(LegacySocket* socket, sa_family_t family);
gint legacysocket_connectToPeer(LegacySocket* socket, const Host* host, in_addr_t ip,
                                in_port_t port, sa_family_t family);

gboolean legacysocket_isUnix(LegacySocket* socket);
void legacysocket_setUnix(LegacySocket* socket, gboolean isUnixSocket);
void legacysocket_setUnixPath(LegacySocket* socket, const gchar* path, gboolean isBound);
gchar* legacysocket_getUnixPath(LegacySocket* socket);

#endif /* SHD_SOCKET_H_ */
