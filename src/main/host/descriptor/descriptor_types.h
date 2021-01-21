/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_
#define SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_

#include <glib.h>

#include "main/host/status.h"
#include "main/utility/utility.h"

typedef enum _LegacyDescriptorType LegacyDescriptorType;
enum _LegacyDescriptorType {
    DT_NONE,
    DT_TCPSOCKET,
    DT_UDPSOCKET,
    DT_PIPE,
    DT_UNIXSOCKET,
    DT_EPOLL,
    DT_EVENTD,
    DT_TIMER,
    DT_FILE,
};

typedef struct _LegacyDescriptor LegacyDescriptor;
typedef struct _DescriptorFunctionTable DescriptorFunctionTable;

/* required functions */

/* Returns TRUE if the descriptor should be deregistered from the owning
 * process upon return from the function, FALSE if the child will handle
 * deregistration on its own. */
typedef gboolean (*DescriptorCloseFunc)(LegacyDescriptor* descriptor);
typedef void (*DescriptorFreeFunc)(LegacyDescriptor* descriptor);

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _DescriptorFunctionTable {
    DescriptorCloseFunc close;
    DescriptorFreeFunc free;
    MAGIC_DECLARE;
};

struct _LegacyDescriptor {
    DescriptorFunctionTable* funcTable;
    Process* ownerProcess;
    gint handle;
    LegacyDescriptorType type;
    Status status;
    GHashTable* listeners;
    gint referenceCount;
    gint flags;
    MAGIC_DECLARE;
};

#endif /* SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_ */
