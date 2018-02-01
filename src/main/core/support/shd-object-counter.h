/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_CORE_SUPPORT_SHD_OBJECT_COUNTER_H_
#define SRC_MAIN_CORE_SUPPORT_SHD_OBJECT_COUNTER_H_

typedef enum _ObjectType ObjectType;
enum _ObjectType {
    OBJECT_TYPE_NONE,
    OBJECT_TYPE_EVENT,
    OBJECT_TYPE_TASK,
    OBJECT_TYPE_PACKET,
    OBJECT_TYPE_DESCRIPTOR,
    OBJECT_TYPE_TCP,
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
void objectcounter_increment(ObjectCounter* counter, ObjectType otype, CounterType ctype);

/* prints the current state of the counters as a string that can be logged.
 * the string is owned by the object counter, and should not be freed by the caller. */
const gchar* objectcounter_toString(ObjectCounter* counter);

#endif /* SRC_MAIN_CORE_SUPPORT_SHD_OBJECT_COUNTER_H_ */
