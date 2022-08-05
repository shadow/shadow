/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/core/work/event.h"

#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/cpu.h"
#include "main/host/host.h"
#include "main/host/tracker.h"
#include "main/utility/utility.h"

struct _Event {
    GQuark dstHostID;
    GQuark srcHostID;
    TaskRef* task;
    SimulationTime time;
    guint64 srcHostEventID;
    gint referenceCount;
    MAGIC_DECLARE;
};

Event* event_new_(TaskRef* task, SimulationTime time, Host* srcHost, GQuark dstHostID) {
    utility_assert(task != NULL);
    Event* event = g_new0(Event, 1);
    MAGIC_INIT(event);

    event->srcHostID = host_getID(srcHost);
    event->dstHostID = dstHostID;
    event->task = taskref_clone(task);
    event->time = time;
    event->srcHostEventID = host_getNewEventID(srcHost);
    event->referenceCount = 1;

    worker_count_allocation(Event);
    return event;
}

static void _event_free(Event* event) {
    taskref_drop(event->task);
    MAGIC_CLEAR(event);
    g_free(event);
    worker_count_deallocation(Event);
}

void event_ref(Event* event) {
    MAGIC_ASSERT(event);
    event->referenceCount++;
}

void event_unref(Event* event) {
    MAGIC_ASSERT(event);
    event->referenceCount--;
    if(event->referenceCount <= 0) {
        _event_free(event);
    }
}

void event_execute(Event* event, Host* host) {
    MAGIC_ASSERT(event);

    utility_assert(event_getHostID(event) == host_getID(host));

    /* check if we are allowed to execute or have to wait for cpu delays */
    CPU* cpu = host_getCPU(host);
    cpu_updateTime(cpu, event->time);

    if(cpu_isBlocked(cpu)) {
        SimulationTime cpuDelay = cpu_getDelay(cpu);
        trace("event blocked on CPU, rescheduled for %"G_GUINT64_FORMAT" nanoseconds from now", cpuDelay);

        /* track the event delay time */
        Tracker* tracker = host_getTracker(host);
        if (tracker != NULL) {
            tracker_addVirtualProcessingDelay(tracker, cpuDelay);
        }

        /* this event is delayed due to cpu, so reschedule it to ourselves */
        worker_scheduleTaskWithDelay(event->task, host, cpuDelay);
    } else {
        /* cpu is not blocked, its ok to execute the event */
        host_continueExecutionTimer(host);
        taskref_execute(event->task, host);
        host_stopExecutionTimer(host);
    }
}

SimulationTime event_getTime(Event* event) {
    MAGIC_ASSERT(event);
    return event->time;
}

GQuark event_getHostID(Event* event) {
    MAGIC_ASSERT(event);
    return event->dstHostID;
}

void event_setTime(Event* event, SimulationTime time) {
    MAGIC_ASSERT(event);
    event->time = time;
}

gint event_compare(const Event* a, const Event* b, gpointer userData) {
    MAGIC_ASSERT(a);
    MAGIC_ASSERT(b);

    /* Shadow events should be scheduled in a way that preserves deterministic behavior.
     * The priority order is:
     *  - time (the sim time that the event will occur)
     *  - dst host id (where the packet is going to)
     *  - src host id (where the packet came from)
     *  - sequence in which the event was pushed (in case src hosts and dst hosts both match)
     *  (Host ids are guaranteed to be unique across hosts.)
     */
    if (a->time > b->time) {
        return 1;
    } else if (a->time < b->time) {
        return -1;
    } else {
        if (a->dstHostID > b->dstHostID) {
            return 1;
        } else if (a->dstHostID < b->dstHostID) {
            return -1;
        } else {
            if (a->srcHostID > b->srcHostID) {
                return 1;
            } else if (a->srcHostID < b->srcHostID) {
                return -1;
            } else {
                /* src and dst host are the same. the event should be sorted in
                 * the order that the events were created on the host. */
                if (a->srcHostEventID > b->srcHostEventID) {
                    return 1;
                } else if (a->srcHostEventID < b->srcHostEventID) {
                    return -1;
                } else {
                    /* if the eventIDs are the same, then the two pointers
                     * really are pointing to the same event. */
                    return 0;
                }
            }
        }
    }
}
