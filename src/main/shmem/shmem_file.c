#include "main/shmem/shmem_file.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "main/shmem/shmem_util.h"

static void _shmemfile_getName(size_t nbytes, char* str) {
    assert(str != NULL && nbytes >= 3);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    snprintf(str, MIN(SHD_SHMEM_FILE_NAME_NBYTES, nbytes),
             "/shd_shmemfile_%llu.%llu", (unsigned long long)ts.tv_sec,
             (unsigned long long)ts.tv_nsec);
}

static size_t _shmemfile_roundUpToMultiple(size_t x, size_t multiple) {
    assert(multiple != 0);
    return ((x + multiple - 1) / multiple) * multiple;
}

static size_t _shmemfile_systemPageNBytes() {
    return (size_t)sysconf(_SC_PAGESIZE);
}

int shmemfile_alloc(size_t nbytes, ShMemFile* shmf) {
    if (nbytes == 0 || nbytes % _shmemfile_systemPageNBytes() != 0) {
        SHD_SHMEM_LOG_ERROR(
            "ShMemFile size must be a positive multiple of %zu but requested "
            "size was %zu",
            _shmemfile_systemPageNBytes(), nbytes);

        return -1;
    }

    if (shmf == NULL) {
        SHD_SHMEM_LOG_ERROR("shmf must not be null");
        return -1;
    }

    memset(shmf, 0, sizeof(ShMemFile));

    _shmemfile_getName(SHD_SHMEM_FILE_NAME_NBYTES, shmf->name);

    bool bad = false;

    int fd = shm_open(shmf->name, O_RDWR | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG);

    if (fd >= 0) {
        int rc = ftruncate(fd, nbytes);
        if (rc == 0) {
            void* p = mmap(NULL, nbytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_SHARED, fd, 0);

            if (p != MAP_FAILED) {
                shmf->p = p;
                shmf->nbytes = nbytes;
            } else {
                SHD_SHMEM_LOG_ERROR("error on mmap: %s", strerror(errno));
                bad = true;
            }

        } else { // failed truncate
            SHD_SHMEM_LOG_ERROR("error on truncate: %s", strerror(errno));
            bad = true;
        }

        close(fd);

        if (bad) {
            rc = shm_unlink(shmf->name);
            assert(rc == 0);
        }
    } else {
        bad = true;
        SHD_SHMEM_LOG_ERROR("error on shm_open: %s", strerror(errno));
    }

    return -1 * (bad);
}

// rwails: cleanup redundant logic
int shmemfile_map(const char* name, size_t nbytes, ShMemFile* shmf) {
    if (nbytes == 0 || nbytes % _shmemfile_systemPageNBytes() != 0) {
        SHD_SHMEM_LOG_ERROR(
            "ShMemFile size must be a positive multiple of %zu but requested "
            "size was %zu",
            _shmemfile_systemPageNBytes(), nbytes);

        return -1;
    }

    if (shmf == NULL) {
        SHD_SHMEM_LOG_ERROR("shmf must not be null");
        return -1;
    }

    int rc = 0;
    memset(shmf, 0, sizeof(ShMemFile));
    strncpy(shmf->name, name, SHD_SHMEM_FILE_NAME_NBYTES);

    bool bad = false;

    int fd = shm_open(shmf->name, O_RDWR, S_IRWXU | S_IRWXG);

    if (fd >= 0) {

        void* p = mmap(NULL, nbytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_SHARED, fd, 0);

        if (p != MAP_FAILED) {
            shmf->p = p;
            shmf->nbytes = nbytes;
        } else {
            SHD_SHMEM_LOG_ERROR("error on mmap: %s", strerror(errno));
            bad = true;
        }

        close(fd);

        if (bad) {
            rc = shm_unlink(shmf->name);
            assert(rc == 0);
        }
    } else {
        bad = true;
        SHD_SHMEM_LOG_ERROR("error on shm_open: %s", strerror(errno));
    }

    return -1 * (bad);
}

int shmemfile_unmap(ShMemFile *shmf) {
    int rc = munmap(shmf->p, shmf->nbytes);
    if (rc) {
        SHD_SHMEM_LOG_ERROR("error on munmap: %s", strerror(errno));
    }
    return rc;
}

int shmemfile_free(ShMemFile* shmf) {
    int rc = shmemfile_unmap(shmf);
    if (rc == 0) {
        rc = shm_unlink(shmf->name);
        if (rc) {
            SHD_SHMEM_LOG_ERROR("error on shm_unlink: %s", strerror(errno));
        }
    }

    return rc;
}

size_t shmemfile_goodSizeNBytes(size_t requested_nbytes) {
    return _shmemfile_roundUpToMultiple(requested_nbytes,
                                        _shmemfile_systemPageNBytes());
}
