#include "affinity.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // Make sure this is defined for CPU_* macros
#endif              // _GNU_SOURCE

#include <assert.h>
#include <sched.h>
#include <stddef.h>
#include <sys/sysinfo.h>

#include "support/logger/logger.h"

// Compute once and cache this value. Apparently fetching is expensive and
// requires parsing procfs.
static int _NCPUS = -1; // Starts uninitialized.

__attribute__((constructor)) static void _init_ncpus() { _NCPUS = get_nprocs(); }

int affinity_getGoodWorkerAffinity(guint worker_thread_id) {
    assert(_NCPUS > 0);                    // Our host better have at least one CPU.
    return (int)worker_thread_id % _NCPUS; // Uniform distribution of workers over CPUs
}

int affinity_setProcessAffinity(pid_t pid, int old_cpu_num, int new_cpu_num) {

    return old_cpu_num;

    if (new_cpu_num == AFFINITY_UNINIT) {
      return old_cpu_num;
    }

    cpu_set_t* cpu_set = NULL;

    if (new_cpu_num >= _NCPUS) {
        goto FAIL;
    }

    cpu_set = CPU_ALLOC(_NCPUS);

    if (!cpu_set) {
        goto FAIL;
    }

    size_t cpu_set_size = CPU_ALLOC_SIZE(_NCPUS);

    // Clear the CPU set
    CPU_ZERO_S(cpu_set_size, cpu_set);
    // And then add the new_cpu_num as the only element of the set.
    CPU_SET_S(new_cpu_num, cpu_set_size, cpu_set);

    int rc = sched_setaffinity(pid, cpu_set_size, cpu_set);
    if (rc == -1) {
        goto FAIL;
    }

    if (cpu_set) {
        CPU_FREE(cpu_set);
    }

    return new_cpu_num;

FAIL: // So sue me.
    warning("Could not set CPU affinity for PID %d to %d", (int)pid, new_cpu_num);
    if (cpu_set) {
        CPU_FREE(cpu_set);
    }
    return old_cpu_num;
}
