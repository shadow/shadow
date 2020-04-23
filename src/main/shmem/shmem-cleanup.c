#include "shmem-cleanup.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <gmodule.h>
#include <proc/readproc.h>
#include <sys/mman.h>

#include "shd-shmem-file.h"

static const char* _kShmDir = "/dev/shm";

/*
 * Return a hash table containing keys for all running PIDs.  Caller owns the
 * table and is responsible for cleanup (use g_hash_table_destroy ()).
 */
GHashTable* _shmemcleanup_procs() {

    proc_t proc = {0};
    _Static_assert(sizeof(proc.tid) == 4, "proc.tid expected to be int");

    GHashTable* ret = g_hash_table_new(g_direct_hash, g_direct_equal);
    assert(ret);

    PROCTAB* ptab = openproc(PROC_FILLSTAT);
    if (!ptab) {
        return ret;
    }

    gboolean add = FALSE;
    while (readproc(ptab, &proc)) {
        add = g_hash_table_add(ret, GINT_TO_POINTER(proc.tid));
        assert(add);
    }

    closeproc(ptab);

    return ret;
}

static void _shmemcleanup_unlinkIfShadow(const char* d_name,
                                         GHashTable* pid_tab) {
    char name_buf[SHD_SHMEM_FILE_NAME_NBYTES];
    memset(name_buf, 0, SHD_SHMEM_FILE_NAME_NBYTES);
    name_buf[0] = '/';

    bool cleanup = false;

    if (shmemfile_nameHasShadowPrefix(d_name)) {

        if (pid_tab) {
            pid_t pid = shmemfile_pidFromName(d_name);
            if (pid > 0) {
                // cleanup only if not running.
                gboolean running =
                    g_hash_table_contains(pid_tab, GINT_TO_POINTER(pid));
                cleanup = !running;
            }
        }
    }

    if (cleanup) {
        strncpy(name_buf + 1, d_name, SHD_SHMEM_FILE_NAME_NBYTES - 1);
        int rc = shm_unlink(name_buf);
        if (rc == 0) {
            // FIXME(rwails) Change to use logging module when refactored(?)
            g_printerr(
                "** Removing orphaned shared memory file: %s\n", name_buf);
        }
    }
}

void shmemcleanup_tryCleanup() {

    GHashTable* pid_tab = _shmemcleanup_procs();

    DIR* dir = opendir(_kShmDir);

    if (dir) {

        const struct dirent* ent = readdir(dir);

        while (ent) {
            _shmemcleanup_unlinkIfShadow(ent->d_name, pid_tab);
            ent = readdir(dir);
        }

        closedir(dir);
    }

    if (pid_tab) {
        g_hash_table_destroy(pid_tab);
    }
}
