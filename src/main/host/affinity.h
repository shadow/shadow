#ifndef AFFINITY_H_
#define AFFINITY_H_

#include <glib.h>
#include <sched.h>
#include <sys/types.h>

enum { AFFINITY_UNINIT = -1 };

/*
 * Returns a good CPU number affinity for a worker thread with the given thread id.
 *
 * THREAD SAFETY: thread-safe.
 */
int affinity_getGoodWorkerAffinity(guint worker_thread_id);

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
 * Helper function. Same semantics as affinity_setProcessAffinity but sets the
 * affinity of the calling thread/process.
 */
static inline int affinity_setThisProcessAffinity(int new_cpu_num, int old_cpu_num) {
  return affinity_setProcessAffinity(0, new_cpu_num, old_cpu_num);
}

#endif // AFFINITY_H_
