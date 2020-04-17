/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "core/work/shd-event.h"

#include <stddef.h>

#include "core/logger/shd-logger.h"
#include "core/shd-worker.h"
#include "core/support/shd-object-counter.h"
#include "host/shd-cpu.h"
#include "host/shd-host.h"
#include "host/shd-tracker.h"
#include "utility/shd-utility.h"

struct _Event {
    Host* srcHost;
    Host* dstHost;
    Task* task;
    SimulationTime time;
    guint64 srcHostEventID;
    gint referenceCount;
    MAGIC_DECLARE;
};

Event* event_new_(Task* task, SimulationTime time, gpointer srcHost, gpointer dstHost) {
    utility_assert(task != NULL);
    Event* event = g_new0(Event, 1);
    MAGIC_INIT(event);

    event->srcHost = (Host*)srcHost;
    event->dstHost = (Host*)dstHost;
    event->task = task;
    task_ref(event->task);
    event->time = time;
    event->srcHostEventID = host_getNewEventID(srcHost);
    event->referenceCount = 1;

    worker_countObject(OBJECT_TYPE_EVENT, COUNTER_TYPE_NEW);
    return event;
}

static void _event_free(Event* event) {
    task_unref(event->task);
    MAGIC_CLEAR(event);
    g_free(event);
    worker_countObject(OBJECT_TYPE_EVENT, COUNTER_TYPE_FREE);
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

void event_execute(Event* event) {
    MAGIC_ASSERT(event);

    host_lock(event->dstHost);
    worker_setActiveHost(event->dstHost);

    /* check if we are allowed to execute or have to wait for cpu delays */
    CPU* cpu = host_getCPU(event->dstHost);
    cpu_updateTime(cpu, event->time);

    if(cpu_isBlocked(cpu)) {
        SimulationTime cpuDelay = cpu_getDelay(cpu);
        debug("event blocked on CPU, rescheduled for %"G_GUINT64_FORMAT" nanoseconds from now", cpuDelay);

        /* track the event delay time */
        tracker_addVirtualProcessingDelay(host_getTracker(event->dstHost), cpuDelay);

        /* this event is delayed due to cpu, so reschedule it to ourselves */
        worker_scheduleTask(event->task, cpuDelay);
    } else {
        /* cpu is not blocked, its ok to execute the event */
        host_continueExecutionTimer(event->dstHost);
        task_execute(event->task);
        host_stopExecutionTimer(event->dstHost);
    }

    worker_setActiveHost(NULL);
    host_unlock(event->dstHost);
}

SimulationTime event_getTime(Event* event) {
    MAGIC_ASSERT(event);
    return event->time;
}

gpointer event_getHost(Event* event) {
    MAGIC_ASSERT(event);
    return event->dstHost;
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
        gint cmpresult = host_compare(a->dstHost, b->dstHost, NULL);
        if (cmpresult > 0) {
            return 1;
        } else if (cmpresult < 0) {
            return -1;
        } else {
            cmpresult = host_compare(a->srcHost, b->srcHost, NULL);
            if (cmpresult > 0) {
                return 1;
            } else if (cmpresult < 0) {
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
