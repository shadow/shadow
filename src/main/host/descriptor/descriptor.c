/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/descriptor.h"

#include <fcntl.h>
#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/utility/utility.h"

void legacyfile_init(LegacyFile* descriptor, LegacyFileType type,
                     LegacyFileFunctionTable* funcTable) {
    utility_debugAssert(descriptor && funcTable);

    MAGIC_INIT(descriptor);
    MAGIC_INIT(funcTable);
    descriptor->funcTable = funcTable;
    descriptor->type = type;
    descriptor->event_source = eventsource_new();
    descriptor->refCountStrong = 1;
    descriptor->refCountWeak = 0;

    trace("Descriptor %p has been initialized now", descriptor);

    worker_count_allocation(LegacyDescriptor);
}

void legacyfile_clear(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    if (descriptor->event_source) {
        eventsource_free(descriptor->event_source);
        descriptor->event_source = NULL;
    }
    MAGIC_CLEAR(descriptor);
}

static void _legacyfile_cleanup(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    if (descriptor->funcTable->cleanup) {
        trace("Descriptor %p calling vtable cleanup now", descriptor);
        descriptor->funcTable->cleanup(descriptor);
    }
}

static void _legacyfile_free(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    trace("Descriptor %p calling vtable free now", descriptor);
    descriptor->funcTable->free(descriptor);

    worker_count_deallocation(LegacyDescriptor);
}

void legacyfile_ref(gpointer data) {
    LegacyFile* descriptor = data;
    MAGIC_ASSERT(descriptor);

    // should not increment the strong count when there are only weak references left
    utility_debugAssert(descriptor->refCountStrong > 0);

    (descriptor->refCountStrong)++;
    trace("Descriptor %p strong_ref++ to %i (weak_ref=%i)", descriptor, descriptor->refCountStrong,
          descriptor->refCountWeak);
}

void legacyfile_unref(gpointer data) {
    LegacyFile* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountStrong)--;
    trace("Descriptor %p strong_ref-- to %i (weak_ref=%i)", descriptor, descriptor->refCountStrong,
          descriptor->refCountWeak);

    utility_debugAssert(descriptor->refCountStrong >= 0);

    if (descriptor->refCountStrong > 0) {
        // there are strong references, so do nothing
        return;
    }

    if (descriptor->refCountWeak > 0) {
        // this was the last strong reference, but there are weak references, so cleanup only
        trace("Descriptor %p kept alive by weak count of %d", descriptor, descriptor->refCountWeak);

        // create a temporary weak reference to prevent the _legacyfile_cleanup() from calling
        // legacyfile_unrefWeak() and running the _legacyfile_free() while still running the
        // _legacyfile_cleanup()
        legacyfile_refWeak(descriptor);
        _legacyfile_cleanup(descriptor);
        legacyfile_unrefWeak(descriptor);

        return;
    }

    // this was the last strong reference and no weak references, so cleanup and free
    _legacyfile_cleanup(descriptor);
    _legacyfile_free(descriptor);
}

void legacyfile_refWeak(gpointer data) {
    LegacyFile* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountWeak)++;
    trace("Descriptor %p weak_ref++ to %i (strong_ref=%i)", descriptor, descriptor->refCountWeak,
          descriptor->refCountStrong);
}

void legacyfile_unrefWeak(gpointer data) {
    LegacyFile* descriptor = data;
    MAGIC_ASSERT(descriptor);

    (descriptor->refCountWeak)--;
    trace("Descriptor %p weak_ref-- to %i (strong_ref=%i)", descriptor, descriptor->refCountWeak,
          descriptor->refCountStrong);

    utility_debugAssert(descriptor->refCountWeak >= 0);

    if (descriptor->refCountStrong > 0 || descriptor->refCountWeak > 0) {
        // there are references (strong or weak), so do nothing
        return;
    }

    // this was the last weak reference and no strong references, so we should free
    // _legacyfile_cleanup() should have been called earlier when the strong count reached 0
    _legacyfile_free(descriptor);
}

void legacyfile_close(LegacyFile* descriptor, const Host* host) {
    MAGIC_ASSERT(descriptor);
    MAGIC_ASSERT(descriptor->funcTable);

    // if it's already closed, exit early
    if ((legacyfile_getStatus(descriptor) & FileState_CLOSED) != 0) {
        warning("Attempting to close an already-closed descriptor");
        return;
    }

    trace("Descriptor %p calling vtable close now", descriptor);
    legacyfile_adjustStatus(descriptor, FileState_CLOSED, TRUE, 0);

    descriptor->funcTable->close(descriptor, host);
}

LegacyFileType legacyfile_getType(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->type;
}

const RootedRefCell_StateEventSource* legacyfile_getEventSource(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->event_source;
}

#ifdef DEBUG
static gchar* _legacyfile_statusToString(Status ds) {
    GString* string = g_string_new(NULL);
    if (ds & FileState_ACTIVE) {
        g_string_append_printf(string, "ACTIVE|");
    }
    if (ds & FileState_READABLE) {
        g_string_append_printf(string, "READABLE|");
    }
    if (ds & FileState_WRITABLE) {
        g_string_append_printf(string, "WRITEABLE|");
    }
    if (ds & FileState_CLOSED) {
        g_string_append_printf(string, "CLOSED|");
    }
    if(string->len == 0) {
        g_string_append_printf(string, "NONE|");
    }
    g_string_truncate(string, string->len-1);
    return g_string_free(string, FALSE);
}
#endif

static void _legacyfile_handleStatusChange(LegacyFile* descriptor, Status oldStatus,
                                           FileSignals signals) {
    MAGIC_ASSERT(descriptor);

    /* Identify which bits changed, if any. */
    Status statusesChanged = descriptor->status ^ oldStatus;

    if (!statusesChanged && !signals) {
        return;
    }

#ifdef DEBUG
    gchar* before = _legacyfile_statusToString(oldStatus);
    gchar* after = _legacyfile_statusToString(descriptor->status);
    trace("Status changed on desc %p, from %s to %s", descriptor, before, after);
    g_free(before);
    g_free(after);
#endif

    notify_listeners_with_global_cb_queue(
        descriptor->event_source, descriptor->status, statusesChanged, signals);
}

void legacyfile_adjustStatus(LegacyFile* descriptor, Status status, gboolean doSetBits,
                             FileSignals signals) {
    MAGIC_ASSERT(descriptor);

    Status oldStatus = descriptor->status;

    /* adjust our status as requested */
    if (doSetBits) {
        /* Set all bits indicated by status */
        descriptor->status |= status;
    } else {
        /* Unset all bits indicated by status */
        descriptor->status &= ~status;
    }

    /* Let helper handle the change. */
    _legacyfile_handleStatusChange(descriptor, oldStatus, signals);
}

Status legacyfile_getStatus(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->status;
}

void legacyfile_addListener(LegacyFile* descriptor, StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    eventsource_addLegacyListener(descriptor->event_source, listener);
}

void legacyfile_removeListener(LegacyFile* descriptor, StatusListener* listener) {
    MAGIC_ASSERT(descriptor);
    eventsource_removeLegacyListener(descriptor->event_source, listener);
}

gint legacyfile_getFlags(LegacyFile* descriptor) {
    MAGIC_ASSERT(descriptor);
    return descriptor->flags;
}

void legacyfile_setFlags(LegacyFile* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    if (flags & O_CLOEXEC) {
        warning("Adding CLOEXEC to legacy file when it should "
                "have been added to the descriptor");
    }
    descriptor->flags = flags;
}

void legacyfile_addFlags(LegacyFile* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    if (flags & O_CLOEXEC) {
        warning("Adding CLOEXEC to legacy file when it should "
                "have been added to the descriptor");
    }
    descriptor->flags |= flags;
}

void legacyfile_removeFlags(LegacyFile* descriptor, gint flags) {
    MAGIC_ASSERT(descriptor);
    descriptor->flags &= ~flags;
}

bool legacyfile_supportsSaRestart(LegacyFile* legacyDesc) {
    switch (legacyDesc->type) {
        case DT_TCPSOCKET:
            // TODO: false if a timeout has been set via setsockopt.
            return true;
        case DT_EPOLL:
        case DT_FILE: return false;
        case DT_NONE:
            panic("Unexpected type DT_NONE");
            break;
            // no default, so compiler will force all cases to be handled.
    };
    panic("Bad type: %d", legacyDesc->type);
}
