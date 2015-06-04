/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Event {
    Host* host;
    Task* task;
    SimulationTime time;
    guint64 sequence;
    gint referenceCount;
    MAGIC_DECLARE;
};

Event* event_new(Task* task, SimulationTime time) {
    utility_assert(task != NULL);
    Event* event = g_new0(Event, 1);
    MAGIC_INIT(event);

    event->host = worker_getCurrentHost();
    event->task = task;
    task_ref(event->task);
    event->time = time;
    event->referenceCount = 1;

    return event;
}

static void _event_free(Event* event) {
    task_unref(event->task);
    MAGIC_CLEAR(event);
    g_free(event);
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

    host_lock(event->host);
    worker_setActiveHost(event->host);

    /* check if we are allowed to execute or have to wait for cpu delays */
    CPU* cpu = host_getCPU(event->host);
    cpu_updateTime(cpu, event->time);

    if(cpu_isBlocked(cpu)) {
        SimulationTime cpuDelay = cpu_getDelay(cpu);
        debug("event blocked on CPU, rescheduled for %"G_GUINT64_FORMAT" nanoseconds from now", cpuDelay);

        /* track the event delay time */
        tracker_addVirtualProcessingDelay(host_getTracker(event->host), cpuDelay);

        /* this event is delayed due to cpu, so reschedule it to ourselves */
        worker_scheduleTask(event->task, cpuDelay);
    } else {
        /* cpu is not blocked, its ok to execute the event */
        task_execute(event->task);
    }

    worker_setActiveHost(NULL);
    host_unlock(event->host);
}

SimulationTime event_getTime(Event* event) {
    MAGIC_ASSERT(event);
    return event->time;
}

void event_setTime(Event* event, SimulationTime time) {
    MAGIC_ASSERT(event);
    event->time = time;
}

void event_setSequence(Event* event, guint64 sequence) {
    MAGIC_ASSERT(event);
    event->sequence = sequence;
}

gint event_compare(const Event* a, const Event* b, gpointer userData) {
    MAGIC_ASSERT(a);
    MAGIC_ASSERT(b);
    /* events already scheduled get priority over new events */
    return (a->time > b->time) ? +1 : (a->time < b->time) ? -1 :
            (a->sequence > b->sequence) ? +1 : (a->sequence < b->sequence) ? -1 : 0;
}
