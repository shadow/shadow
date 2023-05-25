#ifndef SHD_SHMEM_FILE_H_
#define SHD_SHMEM_FILE_H_

/* Intended to be private to shd-shmem-allocator. */

#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#define SHD_SHMEM_FILE_NAME_NBYTES 256 

typedef struct _ShMemFile {
    void* p;
    size_t nbytes;
    char name[SHD_SHMEM_FILE_NAME_NBYTES];
} ShMemFile;

#ifdef __cplusplus
extern "C" {
#endif

int shmemfile_alloc(size_t nbytes, ShMemFile* shmf);

int shmemfile_map(const char* name, size_t nbytes, ShMemFile* shmf);
int shmemfile_unmap(ShMemFile* shmf);

int shmemfile_free(ShMemFile* shmf);

size_t shmemfile_goodSizeNBytes(size_t requested_nbytes);

#ifdef __cplusplus
}
#endif

#endif // SHD_SHMEM_FILE_H_
