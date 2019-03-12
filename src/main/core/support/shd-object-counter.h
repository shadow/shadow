/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_CORE_SUPPORT_SHD_OBJECT_COUNTER_H_
#define SRC_MAIN_CORE_SUPPORT_SHD_OBJECT_COUNTER_H_

typedef enum _ObjectType ObjectType;
enum _ObjectType {
    OBJECT_TYPE_NONE,
    OBJECT_TYPE_TASK,
    OBJECT_TYPE_EVENT,
    OBJECT_TYPE_PACKET,
    OBJECT_TYPE_HOST,
    OBJECT_TYPE_PROCESS,
    OBJECT_TYPE_DESCRIPTOR,
    OBJECT_TYPE_CHANNEL,
    OBJECT_TYPE_TCP,
    OBJECT_TYPE_UDP,
    OBJECT_TYPE_EPOLL,
    OBJECT_TYPE_TIMER,
    OBJECT_TYPE_COMMAND,
};

typedef enum _CounterType CounterType;
enum _CounterType {
    COUNTER_TYPE_NONE,
    COUNTER_TYPE_NEW,
    COUNTER_TYPE_FREE,
};

typedef struct _ObjectCounter ObjectCounter;

ObjectCounter* objectcounter_new();
void objectcounter_free(ObjectCounter* counter);

/* increment the counter of type ctype for the object of type otype. */
void objectcounter_incrementOne(ObjectCounter* counter, ObjectType otype, CounterType ctype);

/* add all counter values from 'increment' into the values of 'counter' */
void objectcounter_incrementAll(ObjectCounter* counter, ObjectCounter* increment);

/* prints the current values of the counters as a string that can be logged.
 * the string is owned by the object counter, and should not be freed by the caller. */
const gchar* objectcounter_valuesToString(ObjectCounter* counter);

/* prints the differences between new and free counters as a string that can be logged.
 * the string is owned by the object counter, and should not be freed by the caller. */
const gchar* objectcounter_diffsToString(ObjectCounter* counter);

#endif /* SRC_MAIN_CORE_SUPPORT_SHD_OBJECT_COUNTER_H_ */
