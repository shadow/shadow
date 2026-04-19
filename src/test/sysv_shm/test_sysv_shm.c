/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

static int fail_errno(const char* msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    return EXIT_FAILURE;
}

static int fail_msg(const char* msg) {
    fprintf(stderr, "%s\n", msg);
    return EXIT_FAILURE;
}

static int parse_key(const char* s, key_t* out) {
    char* endptr = NULL;
    long value = strtol(s, &endptr, 0);
    if (s[0] == '\0' || endptr == NULL || *endptr != '\0') {
        return fail_msg("invalid SysV shm key");
    }
    *out = (key_t)value;
    return EXIT_SUCCESS;
}

static void cleanup_key(key_t key) {
    int shmid = shmget(key, 1, 0600);
    if (shmid >= 0) {
        shmctl(shmid, IPC_RMID, NULL);
    }
}

static int run_basic(void) {
    int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | IPC_EXCL | 0600);
    if (shmid < 0) {
        return fail_errno("shmget(basic)");
    }

    unsigned char* p = shmat(shmid, NULL, 0);
    if (p == (void*)-1) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmat(basic)");
    }

    p[0] = 0x5a;
    if (p[0] != 0x5a) {
        shmdt(p);
        shmctl(shmid, IPC_RMID, NULL);
        return fail_msg("shared-memory round-trip failed");
    }

    struct shmid_ds ds;
    if (shmctl(shmid, IPC_STAT, &ds) < 0) {
        shmdt(p);
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmctl(IPC_STAT)");
    }
    if (ds.shm_segsz != 4096) {
        shmdt(p);
        shmctl(shmid, IPC_RMID, NULL);
        return fail_msg("unexpected shm_segsz");
    }
    if (ds.shm_nattch < 1) {
        shmdt(p);
        shmctl(shmid, IPC_RMID, NULL);
        return fail_msg("unexpected shm_nattch");
    }

    if (shmdt(p) < 0) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmdt(basic)");
    }
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        return fail_errno("shmctl(IPC_RMID)");
    }

    return EXIT_SUCCESS;
}

static int run_writer(key_t key) {
    cleanup_key(key);

    int shmid = shmget(key, 4096, IPC_CREAT | IPC_EXCL | 0600);
    if (shmid < 0) {
        return fail_errno("shmget(writer)");
    }

    unsigned char* p = shmat(shmid, NULL, 0);
    if (p == (void*)-1) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmat(writer)");
    }

    p[0] = 0x42;
    p[1] = 0;

    sleep(2);

    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        shmdt(p);
        return fail_errno("shmctl(writer IPC_RMID)");
    }

    sleep(4);

    if (p[0] != 0x42) {
        shmdt(p);
        return fail_msg("writer observed wrong byte after IPC_RMID");
    }

    if (shmdt(p) < 0) {
        return fail_errno("shmdt(writer)");
    }

    return EXIT_SUCCESS;
}

static int run_reader(key_t key) {
    int shmid = shmget(key, 4096, 0600);
    if (shmid < 0) {
        return fail_errno("shmget(reader)");
    }

    unsigned char* p = shmat(shmid, NULL, 0);
    if (p == (void*)-1) {
        return fail_errno("shmat(reader)");
    }

    if (p[0] != 0x42) {
        shmdt(p);
        return fail_msg("reader observed wrong byte before IPC_RMID");
    }

    sleep(3);

    if (p[0] != 0x42) {
        shmdt(p);
        return fail_msg("reader observed wrong byte after IPC_RMID");
    }

    if (shmdt(p) < 0) {
        return fail_errno("shmdt(reader)");
    }

    return EXIT_SUCCESS;
}

static int run_owner_perms(key_t key) {
    cleanup_key(key);

    int shmid = shmget(key, 4096, IPC_CREAT | IPC_EXCL | 0000);
    if (shmid < 0) {
        return fail_errno("shmget(owner-perms create)");
    }

    int lookup_shmid = shmget(key, 4096, 0);
    if (lookup_shmid < 0) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmget(owner-perms lookup)");
    }
    if (lookup_shmid != shmid) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_msg("owner-perms lookup returned unexpected shmid");
    }

    unsigned char* p = shmat(lookup_shmid, NULL, 0);
    if (p == (void*)-1) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmat(owner-perms)");
    }

    p[0] = 0x7b;
    if (p[0] != 0x7b) {
        shmdt(p);
        shmctl(shmid, IPC_RMID, NULL);
        return fail_msg("owner-perms round-trip failed");
    }

    if (shmdt(p) < 0) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_errno("shmdt(owner-perms)");
    }
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        return fail_errno("shmctl(owner-perms IPC_RMID)");
    }

    return EXIT_SUCCESS;
}

static int run_isolation_writer(key_t key) {
    cleanup_key(key);

    int shmid = shmget(key, 4096, IPC_CREAT | IPC_EXCL | 0600);
    if (shmid < 0) {
        return fail_errno("shmget(isolation-writer)");
    }

    sleep(4);

    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        return fail_errno("shmctl(isolation-writer IPC_RMID)");
    }

    return EXIT_SUCCESS;
}

static int run_isolation_reader(key_t key) {
    int shmid = shmget(key, 4096, 0600);
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
        return fail_msg("cross-host reader unexpectedly found a SysV shm segment");
    }
    if (errno != ENOENT) {
        return fail_errno("shmget(isolation-reader)");
    }

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return fail_msg("usage: test-sysv-shm <basic|writer|reader|owner-perms|isolation-writer|isolation-reader> [key]");
    }

    if (strcmp(argv[1], "basic") == 0) {
        return run_basic();
    }

    if (argc < 3) {
        return fail_msg("missing SysV shm key");
    }

    key_t key;
    if (parse_key(argv[2], &key) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "writer") == 0) {
        return run_writer(key);
    }
    if (strcmp(argv[1], "reader") == 0) {
        return run_reader(key);
    }
    if (strcmp(argv[1], "owner-perms") == 0) {
        return run_owner_perms(key);
    }
    if (strcmp(argv[1], "isolation-writer") == 0) {
        return run_isolation_writer(key);
    }
    if (strcmp(argv[1], "isolation-reader") == 0) {
        return run_isolation_reader(key);
    }

    return fail_msg("unknown mode");
}
