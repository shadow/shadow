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
        ObjectCounts task;
        ObjectCounts event;
        ObjectCounts packet;
        ObjectCounts payload;
        ObjectCounts router;
        ObjectCounts host;
        ObjectCounts netiface;
        ObjectCounts process;
        ObjectCounts descriptor;
        ObjectCounts channel;
        ObjectCounts tcp;
        ObjectCounts udp;
        ObjectCounts epoll;
        ObjectCounts timer;
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

static void _objectcount_incrementOne(ObjectCounts* counts, CounterType ctype) {
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

static void _objectcount_incrementAll(ObjectCounts* counts, ObjectCounts* increments) {
    utility_assert(counts != NULL);
    utility_assert(increments != NULL);

    counts->new += increments->new;
    counts->free += increments->free;
}

void objectcounter_incrementOne(ObjectCounter* counter, ObjectType otype, CounterType ctype) {
    MAGIC_ASSERT(counter);

    switch(otype) {
        case OBJECT_TYPE_TASK: {
            _objectcount_incrementOne(&(counter->counters.task), ctype);
            break;
        }

        case OBJECT_TYPE_EVENT: {
            _objectcount_incrementOne(&(counter->counters.event), ctype);
            break;
        }

        case OBJECT_TYPE_PACKET: {
            _objectcount_incrementOne(&(counter->counters.packet), ctype);
            break;
        }

        case OBJECT_TYPE_PAYLOAD: {
            _objectcount_incrementOne(&(counter->counters.payload), ctype);
            break;
        }

        case OBJECT_TYPE_ROUTER: {
            _objectcount_incrementOne(&(counter->counters.router), ctype);
            break;
        }

        case OBJECT_TYPE_HOST: {
            _objectcount_incrementOne(&(counter->counters.host), ctype);
            break;
        }

        case OBJECT_TYPE_NETIFACE: {
            _objectcount_incrementOne(&(counter->counters.netiface), ctype);
            break;
        }

        case OBJECT_TYPE_PROCESS: {
            _objectcount_incrementOne(&(counter->counters.process), ctype);
            break;
        }

        case OBJECT_TYPE_DESCRIPTOR: {
            _objectcount_incrementOne(&(counter->counters.descriptor), ctype);
            break;
        }

        case OBJECT_TYPE_CHANNEL: {
            _objectcount_incrementOne(&(counter->counters.channel), ctype);
            break;
        }

        case OBJECT_TYPE_TCP: {
            _objectcount_incrementOne(&(counter->counters.tcp), ctype);
            break;
        }

        case OBJECT_TYPE_UDP: {
            _objectcount_incrementOne(&(counter->counters.udp), ctype);
            break;
        }

        case OBJECT_TYPE_EPOLL: {
            _objectcount_incrementOne(&(counter->counters.epoll), ctype);
            break;
        }

        case OBJECT_TYPE_TIMER: {
            _objectcount_incrementOne(&(counter->counters.timer), ctype);
            break;
        }

        default:
        case OBJECT_TYPE_NONE: {
            break;
        }
    }
}

void objectcounter_incrementAll(ObjectCounter* counter, ObjectCounter* increment) {
    MAGIC_ASSERT(counter);
    MAGIC_ASSERT(increment);
    _objectcount_incrementAll(&(counter->counters.task), &(increment->counters.task));
    _objectcount_incrementAll(&(counter->counters.event), &(increment->counters.event));
    _objectcount_incrementAll(&(counter->counters.packet), &(increment->counters.packet));
    _objectcount_incrementAll(&(counter->counters.payload), &(increment->counters.payload));
    _objectcount_incrementAll(&(counter->counters.router), &(increment->counters.router));
    _objectcount_incrementAll(&(counter->counters.host), &(increment->counters.host));
    _objectcount_incrementAll(&(counter->counters.netiface), &(increment->counters.netiface));
    _objectcount_incrementAll(&(counter->counters.process), &(increment->counters.process));
    _objectcount_incrementAll(&(counter->counters.descriptor), &(increment->counters.descriptor));
    _objectcount_incrementAll(&(counter->counters.channel), &(increment->counters.channel));
    _objectcount_incrementAll(&(counter->counters.tcp), &(increment->counters.tcp));
    _objectcount_incrementAll(&(counter->counters.udp), &(increment->counters.udp));
    _objectcount_incrementAll(&(counter->counters.epoll), &(increment->counters.epoll));
    _objectcount_incrementAll(&(counter->counters.timer), &(increment->counters.timer));
}

const gchar* objectcounter_valuesToString(ObjectCounter* counter) {
    MAGIC_ASSERT(counter);

    if(!counter->stringBuffer) {
        counter->stringBuffer = g_string_new(NULL);
    }

    g_string_printf(counter->stringBuffer, "ObjectCounter: counter values: "
            "task_new=%"G_GUINT64_FORMAT" task_free=%"G_GUINT64_FORMAT" "
            "event_new=%"G_GUINT64_FORMAT" event_free=%"G_GUINT64_FORMAT" "
            "packet_new=%"G_GUINT64_FORMAT" packet_free=%"G_GUINT64_FORMAT" "
            "payload_new=%"G_GUINT64_FORMAT" payload_free=%"G_GUINT64_FORMAT" "
            "router_new=%"G_GUINT64_FORMAT" router_free=%"G_GUINT64_FORMAT" "
            "host_new=%"G_GUINT64_FORMAT" host_free=%"G_GUINT64_FORMAT" "
            "netiface_new=%"G_GUINT64_FORMAT" netiface_free=%"G_GUINT64_FORMAT" "
            "process_new=%"G_GUINT64_FORMAT" process_free=%"G_GUINT64_FORMAT" "
            "descriptor_new=%"G_GUINT64_FORMAT" descriptor_free=%"G_GUINT64_FORMAT" "
            "channel_new=%"G_GUINT64_FORMAT" channel_free=%"G_GUINT64_FORMAT" "
            "tcp_new=%"G_GUINT64_FORMAT" tcp_free=%"G_GUINT64_FORMAT" "
            "udp_new=%"G_GUINT64_FORMAT" udp_free=%"G_GUINT64_FORMAT" "
            "epoll_new=%"G_GUINT64_FORMAT" epoll_free=%"G_GUINT64_FORMAT" "
            "timer_new=%"G_GUINT64_FORMAT" timer_free=%"G_GUINT64_FORMAT" ",
            counter->counters.task.new, counter->counters.task.free,
            counter->counters.event.new, counter->counters.event.free,
            counter->counters.packet.new, counter->counters.packet.free,
            counter->counters.payload.new, counter->counters.payload.free,
            counter->counters.router.new, counter->counters.router.free,
            counter->counters.host.new, counter->counters.host.free,
            counter->counters.netiface.new, counter->counters.netiface.free,
            counter->counters.process.new, counter->counters.process.free,
            counter->counters.descriptor.new, counter->counters.descriptor.free,
            counter->counters.channel.new, counter->counters.channel.free,
            counter->counters.tcp.new, counter->counters.tcp.free,
            counter->counters.udp.new, counter->counters.udp.free,
            counter->counters.epoll.new, counter->counters.epoll.free,
            counter->counters.timer.new, counter->counters.timer.free);

    return (const gchar*) counter->stringBuffer->str;
}

const gchar* objectcounter_diffsToString(ObjectCounter* counter) {
    MAGIC_ASSERT(counter);

    if(!counter->stringBuffer) {
        counter->stringBuffer = g_string_new(NULL);
    }

    g_string_printf(counter->stringBuffer, "ObjectCounter: counter diffs: "
            "task=%"G_GUINT64_FORMAT" "
            "event=%"G_GUINT64_FORMAT" "
            "packet=%"G_GUINT64_FORMAT" "
            "payload=%"G_GUINT64_FORMAT" "
            "router=%"G_GUINT64_FORMAT" "
            "host=%"G_GUINT64_FORMAT" "
            "netiface=%"G_GUINT64_FORMAT" "
            "process=%"G_GUINT64_FORMAT" "
            "descriptor=%"G_GUINT64_FORMAT" "
            "channel=%"G_GUINT64_FORMAT" "
            "tcp=%"G_GUINT64_FORMAT" "
            "udp=%"G_GUINT64_FORMAT" "
            "epoll=%"G_GUINT64_FORMAT" "
            "timer=%"G_GUINT64_FORMAT" ",
            counter->counters.task.new - counter->counters.task.free,
            counter->counters.event.new - counter->counters.event.free,
            counter->counters.packet.new - counter->counters.packet.free,
            counter->counters.payload.new - counter->counters.payload.free,
            counter->counters.router.new - counter->counters.router.free,
            counter->counters.host.new - counter->counters.host.free,
            counter->counters.netiface.new - counter->counters.netiface.free,
            counter->counters.process.new - counter->counters.process.free,
            counter->counters.descriptor.new - counter->counters.descriptor.free,
            counter->counters.channel.new - counter->counters.channel.free,
            counter->counters.tcp.new - counter->counters.tcp.free,
            counter->counters.udp.new - counter->counters.udp.free,
            counter->counters.epoll.new - counter->counters.epoll.free,
            counter->counters.timer.new - counter->counters.timer.free);

    return (const gchar*) counter->stringBuffer->str;
}
