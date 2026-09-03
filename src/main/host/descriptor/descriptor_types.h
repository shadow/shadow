/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_
#define SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_

#include <sys/stat.h>
#include <sys/types.h>

#include "lib/linux-api/linux-api.h"
#include "main/bindings/c/bindings-opaque.h"

typedef enum _LegacyFileType LegacyFileType;
enum _LegacyFileType {
    DT_NONE,
    DT_TCPSOCKET,
    DT_EPOLL,
    DT_FILE,
};

typedef struct _LegacyFile LegacyFile;
typedef struct _LegacyFileFunctionTable LegacyFileFunctionTable;

/* required functions */

/* Set "file status" oflags */
typedef void (*LegacyFileSetStatusFlags)(LegacyFile* descriptor, int flags);
/* Get "file status" and "access mode" oflags. */
typedef int (*LegacyFileGetStatusFlags)(LegacyFile* descriptor);
typedef int (*LegacyFileFStatFunc)(LegacyFile* descriptor, struct linux_stat* statbuf);
typedef off_t (*LegacyFileLSeekFunc)(LegacyFile* descriptor, off_t offset, int whence);
typedef void (*LegacyFileCloseFunc)(LegacyFile* descriptor, const Host* host);
typedef void (*LegacyFileCleanupFunc)(LegacyFile* descriptor);
typedef void (*LegacyFileFreeFunc)(LegacyFile* descriptor);

#include "main/core/definitions.h"
#include "main/utility/utility.h"

/*
 * Virtual function table for base descriptor, storing pointers to required
 * callable functions.
 */
struct _LegacyFileFunctionTable {
    LegacyFileSetStatusFlags set_status_flags;
    LegacyFileGetStatusFlags get_status_flags;
    LegacyFileFStatFunc fstat;
    LegacyFileLSeekFunc lseek;
    LegacyFileCloseFunc close;
    LegacyFileCleanupFunc cleanup;
    LegacyFileFreeFunc free;
    MAGIC_DECLARE;
};

struct _LegacyFile {
    LegacyFileFunctionTable* funcTable;
    LegacyFileType type;
    FileState status;
    RootedRefCell_StateEventSource* event_source;
    int refCountStrong;
    int refCountWeak;
    // The "access" and "file creation" oflags; immutable after initialization.
    int accessAndCreationFlags;
    // Since this structure is shared with Rust, we should always include the magic struct
    // member so that the struct is always the same size regardless of compile-time options.
    MAGIC_DECLARE_ALWAYS;
};

#endif /* SRC_MAIN_HOST_DESCRIPTOR_DESCRIPTOR_TYPES_H_ */
