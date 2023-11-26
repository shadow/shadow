/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_COMPAT_SOCKET_H_
#define SRC_MAIN_HOST_DESCRIPTOR_COMPAT_SOCKET_H_

typedef enum _CompatSocketTypes CompatSocketTypes;
typedef union _CompatSocketObject CompatSocketObject;
typedef struct _CompatSocket CompatSocket;

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/descriptor/socket.h"

enum _CompatSocketTypes {
    CST_NONE,
    CST_LEGACY_SOCKET,
    CST_INET_SOCKET,
};

union _CompatSocketObject {
    LegacySocket* as_legacy_socket;
    const InetSocket* as_inet_socket;
};

struct _CompatSocket {
    CompatSocketTypes type;
    CompatSocketObject object;
};

CompatSocket compatsocket_fromLegacySocket(LegacySocket* socket);
CompatSocket compatsocket_fromInetSocket(const InetSocket* socket);

/* reference counting */
CompatSocket compatsocket_refAs(const CompatSocket* socket);
void compatsocket_unref(const CompatSocket* socket);

/* handle to the socket object */
uintptr_t compatsocket_getCanonicalHandle(const CompatSocket* socket);

/* compatability wrappers */
int compatsocket_peekNextPacketPriority(const CompatSocket* socket, uint64_t* priorityOut);
bool compatsocket_hasDataToSend(const CompatSocket* socket);
void compatsocket_pushInPacket(const CompatSocket* socket, const Host* host, Packet* packet,
                               CEmulatedTime recvTime);
Packet* compatsocket_pullOutPacket(const CompatSocket* socket, const Host* host);

#endif /* SRC_MAIN_HOST_DESCRIPTOR_COMPAT_SOCKET_H_ */
