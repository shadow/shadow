/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_
#define SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_

#include <glib.h>

#include "main/utility/utility.h"

/* Bitfield representing possible status types and their states. */
typedef enum _Status Status;
enum _Status {
    STATUS_NONE = 0,
    /* the descriptor has been initialized and it is now OK to
     * unblock any plugin waiting on a particular status  */
    STATUS_DESCRIPTOR_ACTIVE = 1 << 0,
    /* can be read, i.e. there is data waiting for user */
    STATUS_DESCRIPTOR_READABLE = 1 << 1,
    /* can be written, i.e. there is available buffer space */
    STATUS_DESCRIPTOR_WRITABLE = 1 << 2,
    /* user already called close */
    STATUS_DESCRIPTOR_CLOSED = 1 << 3,
};

typedef enum _DescriptorType DescriptorType;
enum _DescriptorType {
    DT_NONE,
    DT_TCPSOCKET,
    DT_UDPSOCKET,
    DT_PIPE,
    DT_SOCKETPAIR,
    DT_EPOLL,
    DT_TIMER,
    DT_FILE,
};

typedef struct _Descriptor Descriptor;
typedef struct _DescriptorFunctionTable DescriptorFunctionTable;

/* required functions */

/* Returns TRUE if the descriptor should be deregistered from the owning
 * process upon return from the function, FALSE if the child will handle
 * deregistration on its own. */
typedef gboolean (*DescriptorCloseFunc)(Descriptor* descriptor);
typedef void (*DescriptorFreeFunc)(Descriptor* descriptor);

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _DescriptorFunctionTable {
    DescriptorCloseFunc close;
    DescriptorFreeFunc free;
    MAGIC_DECLARE;
};

struct _Descriptor {
    DescriptorFunctionTable* funcTable;
    Process* ownerProcess;
    gint handle;
    DescriptorType type;
    Status status;
    GHashTable* listeners;
    gint referenceCount;
    gint flags;
    MAGIC_DECLARE;
};

#endif /* SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_ */
