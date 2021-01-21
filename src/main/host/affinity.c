#include "affinity.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // Make sure this is defined for CPU_* macros
#endif              // _GNU_SOURCE

#include <assert.h>
#include <glib.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/sysinfo.h>

#include "support/logger/logger.h"

// Compute once and cache this value. Apparently fetching is expensive and
// requires parsing procfs.
static int _ncpus = -1; // Starts uninitialized.

static gint _affinity_enabled = 0;

__attribute__((constructor)) static void _init_ncpus() { _ncpus = get_nprocs(); }

int affinity_getGoodWorkerAffinity(guint worker_thread_id) {
    assert(_ncpus > 0);                    // Our host better have at least one CPU.
    return (int)worker_thread_id % _ncpus; // Uniform distribution of workers over CPUs
}

int affinity_setProcessAffinity(pid_t pid, int new_cpu_num, int old_cpu_num) {
    assert(_ncpus > 0 && pid >= 0);

    // We can short-circuit if there's no work to do.
    if (!_affinity_enabled || new_cpu_num == AFFINITY_UNINIT || new_cpu_num == old_cpu_num) {
        return old_cpu_num;
    }

    cpu_set_t* cpu_set = NULL;
    bool set_affinity_suceeded = false;
    int retval = new_cpu_num;

    if (new_cpu_num < _ncpus) { // Good, we are trying to set to a valid CPU
        cpu_set = CPU_ALLOC(_ncpus);

        if (cpu_set) { // Good, the CPU set allocation succeeded

            size_t cpu_set_size = CPU_ALLOC_SIZE(_ncpus);

            // Clear the CPU set
            CPU_ZERO_S(cpu_set_size, cpu_set);
            // And then add the new_cpu_num as the only element of the set
            CPU_SET_S(new_cpu_num, cpu_set_size, cpu_set);

            int rc = sched_setaffinity(pid, cpu_set_size, cpu_set);

            set_affinity_suceeded = (rc == 0);
        }
    }

    if (!set_affinity_suceeded) {
        critical("cpu-pin was set, but the CPU affinity for PID %d could not be set to %d",
                 (int)pid, new_cpu_num);
        retval = old_cpu_num;
    }

    if (cpu_set) {
        CPU_FREE(cpu_set);
    }

    return retval;
}
