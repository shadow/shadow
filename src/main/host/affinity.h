#ifndef AFFINITY_H_
#define AFFINITY_H_

#include <glib.h>
#include <sched.h>
#include <sys/types.h>

/*
 * Use AFFINITY_UNINIT as a value specifying that the CPU affinity of the
 * process is not known or not initialized. AFFINITY_UNINIT is a good value to
 * initalize affinity variables with before the affinity has been set with
 * affinity_setProcessAffinity().
 */
enum { AFFINITY_UNINIT = -1 };

/*
 * Returns a good CPU number affinity for the next worker.
 *
 * THREAD SAFETY: Thread-safe.
 */
int affinity_getGoodWorkerAffinity();

/*
 * Try to parse platform CPU orientation information from the host machine.
 *
 * THREAD SAFETY: Not thread-safe. Only call this function once per program
 * execution.
 *
 * RETURN VALUE:
 *
 * 0 if no errors occurred and platform information was successfully
 * initialized. Otherwise returns -1 and emits an error message to the log.
 */
int affinity_initPlatformInfo();

/*
 * Try to set the affinity of the process with the given pid to new_cpu_num. Logs a
 * warning if the attempt was not sucessful.
 *
 * Providing the old_cpu_num allows this function to short-circuit in the event
 * that a CPU migration is not required. Set this parameter to AFFINITY_UNINIT
 * if the process affinity has not yet been set or the current affinity is
 * unknown.
 *
 * Returns the CPU number of the pid after assignment. In other words, if the
 * call was successful, this function returns new_cpu_num. Otherwise, it
 * returns old_cpu_num.
 *
 * THREAD SAFETY: thread-safe.
 */
int affinity_setProcessAffinity(pid_t pid, int new_cpu_num, int old_cpu_num);

/*
 * As `affinity_setProcessAffinity`, but takes a pthread.
 */
int affinity_setPthreadAffinity(pthread_t thread, int new_cpu_num, int old_cpu_num);

/*
 * Helper function. Same semantics as affinity_setProcessAffinity but sets the
 * affinity of the calling thread/process.
 */
static inline int affinity_setThisProcessAffinity(int new_cpu_num, int old_cpu_num) {
    return affinity_setProcessAffinity(0, new_cpu_num, old_cpu_num);
}

#endif // AFFINITY_H_
