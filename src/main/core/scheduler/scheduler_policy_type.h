/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_POLICY_TYPE_H_
#define SHD_SCHEDULER_POLICY_TYPE_H_

typedef enum {
    /* every host has a locked pqueue into which every thread inserts events,
     * max queue contention is N for N threads */
    SP_PARALLEL_HOST_SINGLE,
    /* modified version of SP_PARALLEL_HOST_SINGLE that implements work stealing */
    SP_PARALLEL_HOST_STEAL,
    /* every thread has a locked pqueue into which every thread inserts events,
     * max queue contention is N for N threads */
    SP_PARALLEL_THREAD_SINGLE,
    /* every thread has a locked pqueue for every thread, each thread inserts into its one
     * assigned thread queue and max queue contention is 2 threads at any time */
    SP_PARALLEL_THREAD_PERTHREAD,
    /* every thread has a locked pqueue for every host, each thread inserts into its one
     * assigned host queue and max queue contention is 2 threads at any time */
    SP_PARALLEL_THREAD_PERHOST,
} SchedulerPolicyType;

#endif /* SHD_SCHEDULER_POLICY_TYPE_H_ */
