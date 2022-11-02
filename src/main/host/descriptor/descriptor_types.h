/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_
#define SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_

#include <glib.h>

typedef enum _LegacyFileType LegacyFileType;
enum _LegacyFileType {
    DT_NONE,
    DT_TCPSOCKET,
    DT_UDPSOCKET,
    DT_EPOLL,
    DT_EVENTD,
    DT_TIMER,
    DT_FILE,
};

typedef struct _LegacyFile LegacyFile;
typedef struct _LegacyFileFunctionTable LegacyFileFunctionTable;

/* Including host.h here would introduce a circular dependency.
 * We work around it by forward declaring Host here, and including host.h below
 * to ensure we get an error if host.h's forward declaration somehow changes.
 */
typedef struct Host Host;

/* required functions */
typedef void (*LegacyFileCloseFunc)(LegacyFile* descriptor, const Host* host);
typedef void (*LegacyFileCleanupFunc)(LegacyFile* descriptor);
typedef void (*LegacyFileFreeFunc)(LegacyFile* descriptor);

#include "main/core/support/definitions.h"
#include "main/host/status.h"
#include "main/utility/utility.h"

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _LegacyFileFunctionTable {
    LegacyFileCloseFunc close;
    LegacyFileCleanupFunc cleanup;
    LegacyFileFreeFunc free;
    MAGIC_DECLARE;
};

struct _LegacyFile {
    LegacyFileFunctionTable* funcTable;
    LegacyFileType type;
    Status status;
    GHashTable* listeners;
    gint refCountStrong;
    gint refCountWeak;
    gint flags;
    // Since this structure is shared with Rust, we should always include the magic struct
    // member so that the struct is always the same size regardless of compile-time options.
    MAGIC_DECLARE_ALWAYS;
};

/* Included to ensure our forward declaration of Host is compatible with
 * the canonical one. We can't include this sooner without causing the  build to fail
 * due to circular dependencies.
 */
#include "main/host/host.h"

#endif /* SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_ */
