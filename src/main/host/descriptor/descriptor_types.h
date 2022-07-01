/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_
#define SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_

#include <glib.h>

typedef enum _LegacyDescriptorType LegacyDescriptorType;
enum _LegacyDescriptorType {
    DT_NONE,
    DT_TCPSOCKET,
    DT_UDPSOCKET,
    DT_EPOLL,
    DT_EVENTD,
    DT_TIMER,
    DT_FILE,
};

typedef struct _LegacyDescriptor LegacyDescriptor;
typedef struct _DescriptorFunctionTable DescriptorFunctionTable;

#include "main/core/support/definitions.h"
#include "main/host/status.h"
#include "main/utility/utility.h"

/* required functions */
typedef void (*DescriptorCloseFunc)(LegacyDescriptor* descriptor, Host* host);
typedef void (*DescriptorCleanupFunc)(LegacyDescriptor* descriptor);
typedef void (*DescriptorFreeFunc)(LegacyDescriptor* descriptor);

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _DescriptorFunctionTable {
    DescriptorCloseFunc close;
    DescriptorCleanupFunc cleanup;
    DescriptorFreeFunc free;
    MAGIC_DECLARE;
};

struct _LegacyDescriptor {
    DescriptorFunctionTable* funcTable;
    LegacyDescriptorType type;
    Status status;
    GHashTable* listeners;
    gint refCountStrong;
    gint refCountWeak;
    gint flags;
    // Since this structure is shared with Rust, we should always include the magic struct
    // member so that the struct is always the same size regardless of compile-time options.
    MAGIC_DECLARE_ALWAYS;
};

#endif /* SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_ */
