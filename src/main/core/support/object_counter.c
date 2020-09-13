/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/core/support/object_counter.h"

#include <stddef.h>

#include "main/core/support/definitions.h"
#include "main/utility/utility.h"

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
        ObjectCounts threadpreload;
        ObjectCounts threadptrace;
        ObjectCounts syscallcondition;
        ObjectCounts syscallhandler;
        ObjectCounts descriptorlistener;
        ObjectCounts descriptortable;
        ObjectCounts descriptor;
        ObjectCounts channel;
        ObjectCounts tcp;
        ObjectCounts udp;
        ObjectCounts epoll;
        ObjectCounts timer;
        ObjectCounts file;
        ObjectCounts futex;
        ObjectCounts futextable;
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

    // Disable clang-format to avoid line breaks across counter types
    // clang-format off
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

        case OBJECT_TYPE_THREAD_PRELOAD: {
            _objectcount_incrementOne(
                &(counter->counters.threadpreload), ctype);
            break;
        }

        case OBJECT_TYPE_THREAD_PTRACE: {
            _objectcount_incrementOne(&(counter->counters.threadptrace), ctype);
            break;
        }

        case OBJECT_TYPE_SYSCALL_CONDITION: {
            _objectcount_incrementOne(&(counter->counters.syscallcondition), ctype);
            break;
        }

        case OBJECT_TYPE_SYSCALL_HANDLER: {
            _objectcount_incrementOne(
                &(counter->counters.syscallhandler), ctype);
            break;
        }

        case OBJECT_TYPE_DESCRIPTOR_LISTENER: {
            _objectcount_incrementOne(
                &(counter->counters.descriptorlistener), ctype);
            break;
        }

        case OBJECT_TYPE_DESCRIPTOR_TABLE: {
            _objectcount_incrementOne(
                &(counter->counters.descriptortable), ctype);
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

        case OBJECT_TYPE_FILE: {
            _objectcount_incrementOne(&(counter->counters.file), ctype);
            break;
        }

        case OBJECT_TYPE_FUTEX: {
            _objectcount_incrementOne(&(counter->counters.futex), ctype);
            break;
        }

        case OBJECT_TYPE_FUTEX_TABLE: {
            _objectcount_incrementOne(&(counter->counters.futextable), ctype);
            break;
        }

        default:
        case OBJECT_TYPE_NONE: {
            break;
        }
    }
    // clang-format on
}

void objectcounter_incrementAll(ObjectCounter* counter, ObjectCounter* increment) {
    MAGIC_ASSERT(counter);
    MAGIC_ASSERT(increment);
    // Disable clang-format to avoid line breaks across counter types
    // clang-format off
    _objectcount_incrementAll(&(counter->counters.task),
            &(increment->counters.task));
    _objectcount_incrementAll(&(counter->counters.event),
            &(increment->counters.event));
    _objectcount_incrementAll(&(counter->counters.packet),
            &(increment->counters.packet));
    _objectcount_incrementAll(&(counter->counters.payload),
            &(increment->counters.payload));
    _objectcount_incrementAll(&(counter->counters.router),
            &(increment->counters.router));
    _objectcount_incrementAll(&(counter->counters.host),
            &(increment->counters.host));
    _objectcount_incrementAll(&(counter->counters.netiface),
            &(increment->counters.netiface));
    _objectcount_incrementAll(&(counter->counters.process),
            &(increment->counters.process));
    _objectcount_incrementAll(&(counter->counters.threadpreload),
            &(increment->counters.threadpreload));
    _objectcount_incrementAll(&(counter->counters.threadptrace),
            &(increment->counters.threadptrace));
    _objectcount_incrementAll(&(counter->counters.syscallcondition),
            &(increment->counters.syscallcondition));
    _objectcount_incrementAll(&(counter->counters.syscallhandler),
            &(increment->counters.syscallhandler));
    _objectcount_incrementAll(&(counter->counters.descriptorlistener),
            &(increment->counters.descriptorlistener));
    _objectcount_incrementAll(&(counter->counters.descriptortable),
            &(increment->counters.descriptortable));
    _objectcount_incrementAll(&(counter->counters.descriptor),
            &(increment->counters.descriptor));
    _objectcount_incrementAll(&(counter->counters.channel),
            &(increment->counters.channel));
    _objectcount_incrementAll(&(counter->counters.tcp),
            &(increment->counters.tcp));
    _objectcount_incrementAll(&(counter->counters.udp),
            &(increment->counters.udp));
    _objectcount_incrementAll(&(counter->counters.epoll),
            &(increment->counters.epoll));
    _objectcount_incrementAll(&(counter->counters.timer),
            &(increment->counters.timer));
    _objectcount_incrementAll(&(counter->counters.file),
            &(increment->counters.file));
    _objectcount_incrementAll(&(counter->counters.futex),
            &(increment->counters.futex));
    _objectcount_incrementAll(&(counter->counters.futextable),
            &(increment->counters.futextable));
    // clang-format on
}

const gchar* objectcounter_valuesToString(ObjectCounter* counter) {
    MAGIC_ASSERT(counter);

    if(!counter->stringBuffer) {
        counter->stringBuffer = g_string_new(NULL);
    }

    // Disable clang-format to avoid line breaks in string template
    // clang-format off
    g_string_printf(
        counter->stringBuffer,
        "ObjectCounter: counter values: "
        "task_new=%" G_GUINT64_FORMAT " "
        "task_free=%" G_GUINT64_FORMAT " "
        "event_new=%" G_GUINT64_FORMAT " "
        "event_free=%" G_GUINT64_FORMAT " "
        "packet_new=%" G_GUINT64_FORMAT " "
        "packet_free=%" G_GUINT64_FORMAT " "
        "payload_new=%" G_GUINT64_FORMAT " "
        "payload_free=%" G_GUINT64_FORMAT " "
        "router_new=%" G_GUINT64_FORMAT " "
        "router_free=%" G_GUINT64_FORMAT " "
        "host_new=%" G_GUINT64_FORMAT " "
        "host_free=%" G_GUINT64_FORMAT " "
        "netiface_new=%" G_GUINT64_FORMAT " "
        "netiface_free=%" G_GUINT64_FORMAT " "
        "process_new=%" G_GUINT64_FORMAT " "
        "process_free=%" G_GUINT64_FORMAT " "
        "threadpreload_new=%" G_GUINT64_FORMAT " "
        "threadpreload_free=%" G_GUINT64_FORMAT " "
        "threadptrace_new=%" G_GUINT64_FORMAT " "
        "threadptrace_free=%" G_GUINT64_FORMAT " "
        "syscallcondition_new=%" G_GUINT64_FORMAT " "
        "syscallcondition_free=%" G_GUINT64_FORMAT " "
        "syscallhandler_new=%" G_GUINT64_FORMAT " "
        "syscallhandler_free=%" G_GUINT64_FORMAT " "
        "descriptorlistener_new=%" G_GUINT64_FORMAT " "
        "descriptorlistener_free=%" G_GUINT64_FORMAT " "
        "descriptortable_new=%" G_GUINT64_FORMAT " "
        "descriptortable_free=%" G_GUINT64_FORMAT " "
        "descriptor_new=%" G_GUINT64_FORMAT " "
        "descriptor_free=%" G_GUINT64_FORMAT " "
        "channel_new=%" G_GUINT64_FORMAT " "
        "channel_free=%" G_GUINT64_FORMAT " "
        "tcp_new=%" G_GUINT64_FORMAT " "
        "tcp_free=%" G_GUINT64_FORMAT " "
        "udp_new=%" G_GUINT64_FORMAT " "
        "udp_free=%" G_GUINT64_FORMAT " "
        "epoll_new=%" G_GUINT64_FORMAT " "
        "epoll_free=%" G_GUINT64_FORMAT " "
        "timer_new=%" G_GUINT64_FORMAT " "
        "timer_free=%" G_GUINT64_FORMAT " "
        "file_new=%" G_GUINT64_FORMAT " "
        "file_free=%" G_GUINT64_FORMAT " "
        "futex_new=%" G_GUINT64_FORMAT " "
        "futex_free=%" G_GUINT64_FORMAT " "
        "futextable_new=%" G_GUINT64_FORMAT " "
        "futextable_free=%" G_GUINT64_FORMAT " ",
        counter->counters.task.new,
        counter->counters.task.free,
        counter->counters.event.new,
        counter->counters.event.free,
        counter->counters.packet.new,
        counter->counters.packet.free,
        counter->counters.payload.new,
        counter->counters.payload.free,
        counter->counters.router.new,
        counter->counters.router.free,
        counter->counters.host.new,
        counter->counters.host.free,
        counter->counters.netiface.new,
        counter->counters.netiface.free,
        counter->counters.process.new,
        counter->counters.process.free,
        counter->counters.threadpreload.new,
        counter->counters.threadpreload.free,
        counter->counters.threadptrace.new,
        counter->counters.threadptrace.free,
        counter->counters.syscallcondition.new,
        counter->counters.syscallcondition.free,
        counter->counters.syscallhandler.new,
        counter->counters.syscallhandler.free,
        counter->counters.descriptorlistener.new,
        counter->counters.descriptorlistener.free,
        counter->counters.descriptortable.new,
        counter->counters.descriptortable.free,
        counter->counters.descriptor.new,
        counter->counters.descriptor.free,
        counter->counters.channel.new,
        counter->counters.channel.free,
        counter->counters.tcp.new,
        counter->counters.tcp.free,
        counter->counters.udp.new,
        counter->counters.udp.free,
        counter->counters.epoll.new,
        counter->counters.epoll.free,
        counter->counters.timer.new,
        counter->counters.timer.free,
        counter->counters.file.new,
        counter->counters.file.free,
        counter->counters.futex.new,
        counter->counters.futex.free,
        counter->counters.futextable.new,
        counter->counters.futextable.free);
    // clang-format on

    return (const gchar*) counter->stringBuffer->str;
}

const gchar* objectcounter_diffsToString(ObjectCounter* counter) {
    MAGIC_ASSERT(counter);

    if(!counter->stringBuffer) {
        counter->stringBuffer = g_string_new(NULL);
    }

    // Disable clang-format to avoid line breaks in string template or math
    // clang-format off
    g_string_printf(
        counter->stringBuffer,
        "ObjectCounter: counter diffs: "
        "task=%" G_GUINT64_FORMAT " "
        "event=%" G_GUINT64_FORMAT " "
        "packet=%" G_GUINT64_FORMAT " "
        "payload=%" G_GUINT64_FORMAT " "
        "router=%" G_GUINT64_FORMAT " "
        "host=%" G_GUINT64_FORMAT " "
        "netiface=%" G_GUINT64_FORMAT " "
        "process=%" G_GUINT64_FORMAT " "
        "threadpreload=%" G_GUINT64_FORMAT " "
        "threadptrace=%" G_GUINT64_FORMAT " "
        "syscallcondition=%" G_GUINT64_FORMAT " "
        "syscallhandler=%" G_GUINT64_FORMAT " "
        "descriptorlistener=%" G_GUINT64_FORMAT " "
        "descriptortable=%" G_GUINT64_FORMAT " "
        "descriptor=%" G_GUINT64_FORMAT " "
        "channel=%" G_GUINT64_FORMAT " "
        "tcp=%" G_GUINT64_FORMAT " "
        "udp=%" G_GUINT64_FORMAT " "
        "epoll=%" G_GUINT64_FORMAT " "
        "timer=%" G_GUINT64_FORMAT " "
        "file=%" G_GUINT64_FORMAT " "
        "futex=%" G_GUINT64_FORMAT " "
        "futextable=%" G_GUINT64_FORMAT " ",
        counter->counters.task.new -
            counter->counters.task.free,
        counter->counters.event.new -
            counter->counters.event.free,
        counter->counters.packet.new -
            counter->counters.packet.free,
        counter->counters.payload.new -
            counter->counters.payload.free,
        counter->counters.router.new -
            counter->counters.router.free,
        counter->counters.host.new -
            counter->counters.host.free,
        counter->counters.netiface.new -
            counter->counters.netiface.free,
        counter->counters.process.new -
            counter->counters.process.free,
        counter->counters.threadpreload.new -
            counter->counters.threadpreload.free,
        counter->counters.threadptrace.new -
            counter->counters.threadptrace.free,
        counter->counters.syscallcondition.new -
            counter->counters.syscallcondition.free,
        counter->counters.syscallhandler.new -
            counter->counters.syscallhandler.free,
        counter->counters.descriptorlistener.new -
            counter->counters.descriptorlistener.free,
        counter->counters.descriptortable.new -
            counter->counters.descriptortable.free,
        counter->counters.descriptor.new -
            counter->counters.descriptor.free,
        counter->counters.channel.new -
            counter->counters.channel.free,
        counter->counters.tcp.new -
            counter->counters.tcp.free,
        counter->counters.udp.new -
            counter->counters.udp.free,
        counter->counters.epoll.new -
            counter->counters.epoll.free,
        counter->counters.timer.new -
            counter->counters.timer.free,
        counter->counters.file.new -
            counter->counters.file.free,
        counter->counters.futex.new -
            counter->counters.futex.free,
        counter->counters.futextable.new -
            counter->counters.futextable.free);
    // clang-format on

    return (const gchar*) counter->stringBuffer->str;
}
