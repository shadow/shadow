#include "shmem_cleanup.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <gmodule.h>
#include <sys/mman.h>

#include "shmem_file.h"
#include "support/logger/logger.h"

// On some platforms, including centos 7 and 8, procps is built with
// WITH_SYSTEMD defined, but the header is missing that symbol definition. We
// work around it by injecting the definition into the header.
// see: https://gitlab.com/procps-ng/procps/-/issues/31
#define WITH_SYSTEMD
#include <proc/readproc.h>
#undef WITH_SYSTEMD

static const char* SHM_DIR = "/dev/shm";

/*
 * POST: returns a pointer to hash table that contains int-valued PIDs for
 * running processes on the system, or NULL if this table could not be created.
 * Caller owns the table and is responsible for cleanup (use
 * g_hash_table_destroy()).
 */
static GHashTable* _shmemcleanup_getProcSet() {

    proc_t proc = {0};
    _Static_assert(sizeof(proc.tid) == sizeof(gint),
                   "proc.tid not int-sized on this architecture");

    GHashTable* proc_set = g_hash_table_new(g_direct_hash, g_direct_equal);

    if (proc_set) {
        PROCTAB* ptab = openproc(PROC_FILLSTAT);
        if (!ptab) { // if we can't get the current process table, just return
                     // nothing.
            g_hash_table_destroy(proc_set);
            proc_set = NULL;
            return proc_set;
        }

        bool could_add = false;
        // go through each proc in procfs and add the pid (proc.tid) to our
        // proc set
        while (readproc(ptab, &proc)) {
            could_add = g_hash_table_add(proc_set, GINT_TO_POINTER(proc.tid));
            assert(could_add); // we should never encounter two processes with
                               // the same PID
        }

        closeproc(ptab);
    }

    return proc_set;
}

/*
 * PRE: filename and proc_set are non-NULL. proc_set was generated by
 * _shmemcleanup_getProcSet().
 *
 * POST: if filename corresponds to a shadow shared-memory file without an
 * owning PID, this function will try (and may not succeed) to remove it.
 * Returns if the remove was successful or not.
 */
static bool _shmemcleanup_unlinkIfShadow(const char* filename,
                                         GHashTable* proc_set) {
    bool did_remove = false;

    char name_buf[SHD_SHMEM_FILE_NAME_NBYTES];
    memset(name_buf, 0, SHD_SHMEM_FILE_NAME_NBYTES);
    name_buf[0] = '/';

    bool cleanup = false;

    if (shmemfile_nameHasShadowPrefix(filename)) {

        if (proc_set) {
            pid_t pid = shmemfile_pidFromName(filename);
            if (pid > 0) {
                // cleanup only if not running.
                gboolean running =
                    g_hash_table_contains(proc_set, GINT_TO_POINTER(pid));
                cleanup = !running;
            }
        }
    }

    if (cleanup) {
        strncpy(name_buf + 1, filename, SHD_SHMEM_FILE_NAME_NBYTES - 1);
        int rc = shm_unlink(name_buf);
        if (rc == 0) {
            info("Removing orphaned shared memory file: %s", name_buf);
            did_remove = true;
        }
    }

    return did_remove;
}

void shmemcleanup_tryCleanup() {

    GHashTable* proc_set = _shmemcleanup_getProcSet();

    if (!proc_set) {
        return;
    }

    info(
        "Num. processes in system's procfs: %u", g_hash_table_size(proc_set));

    // If we can get a list of running processes on the machine, iterate
    // through the files in shared memory and try to remove them.

    DIR* dir = opendir(SHM_DIR);

    size_t n_removed = 0;

    if (dir) {

        const struct dirent* ent = readdir(dir);

        while (ent) {
            bool did_remove = _shmemcleanup_unlinkIfShadow(
                ent->d_name, proc_set);

            if (did_remove) {
                ++n_removed;
            }
            ent = readdir(dir);
        }

        closedir(dir);
    }

    info("Num. removed shared memory files: %zu", n_removed);

    g_hash_table_destroy(proc_set);
}
