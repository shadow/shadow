/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef struct _ObjectCounts ObjectCounts;
struct _ObjectCounts {
    guint64 new;
    guint64 free;
};

struct _ObjectCounter {
    /* counting objects for debugging memory leaks */
    struct {
        ObjectCounts event;
        ObjectCounts task;
        ObjectCounts packet;
        ObjectCounts tcp;
        ObjectCounts descriptor;
    } counters;

    GString* stringBuffer;

    MAGIC_DECLARE;
};

ObjectCounter* objectcounter_new() {
    ObjectCounter* counter = g_new0(ObjectCounter, 1);
    MAGIC_INIT(counter);
    return counter;
}

void objectcounter_free(ObjectCounter* counter) {
    MAGIC_ASSERT(counter);

    if(counter->stringBuffer) {
        g_string_free(counter->stringBuffer, TRUE);
        counter->stringBuffer = NULL;
    }

    MAGIC_CLEAR(counter);
    g_free(counter);
}

static void _objectcount_incrementCount(ObjectCounts* counts, CounterType ctype) {
    utility_assert(counts != NULL);

    switch(ctype) {
        case COUNTER_TYPE_NEW: {
            counts->new++;
            break;
        }

        case COUNTER_TYPE_FREE: {
            counts->free++;
            break;
        }

        default:
        case COUNTER_TYPE_NONE: {
            break;
        }
    }
}

void objectcounter_increment(ObjectCounter* counter, ObjectType otype, CounterType ctype) {
    MAGIC_ASSERT(counter);

    switch(otype) {
        case OBJECT_TYPE_TASK: {
            _objectcount_incrementCount(&(counter->counters.task), ctype);
            break;
        }

        case OBJECT_TYPE_EVENT: {
            _objectcount_incrementCount(&(counter->counters.event), ctype);
            break;
        }

        case OBJECT_TYPE_PACKET: {
            _objectcount_incrementCount(&(counter->counters.packet), ctype);
            break;
        }

        case OBJECT_TYPE_DESCRIPTOR: {
            _objectcount_incrementCount(&(counter->counters.descriptor), ctype);
            break;
        }

        case OBJECT_TYPE_TCP: {
            _objectcount_incrementCount(&(counter->counters.tcp), ctype);
            break;
        }

        default:
        case OBJECT_TYPE_NONE: {
            break;
        }
    }
}

const gchar* objectcounter_toString(ObjectCounter* counter) {
    MAGIC_ASSERT(counter);

    if(!counter->stringBuffer) {
        counter->stringBuffer = g_string_new(NULL);
    }

    g_string_printf(counter->stringBuffer, "ObjectCounter: state of counters: "
            "task_new=%"G_GUINT64_FORMAT" task_free=%"G_GUINT64_FORMAT" "
            "event_new=%"G_GUINT64_FORMAT" event_free=%"G_GUINT64_FORMAT" "
            "packet_new=%"G_GUINT64_FORMAT" packet_free=%"G_GUINT64_FORMAT" "
            "descriptor_new=%"G_GUINT64_FORMAT" descriptor_free=%"G_GUINT64_FORMAT" "
            "tcp_new=%"G_GUINT64_FORMAT" tcp_free=%"G_GUINT64_FORMAT" ",
            counter->counters.task.new, counter->counters.task.free,
            counter->counters.event.new, counter->counters.event.free,
            counter->counters.packet.new, counter->counters.packet.free,
            counter->counters.descriptor.new, counter->counters.descriptor.free,
            counter->counters.tcp.new, counter->counters.tcp.free);

    return (const gchar*) counter->stringBuffer->str;
}

