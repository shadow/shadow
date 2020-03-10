#ifndef SHD_SHMEM_FILE_H_
#define SHD_SHMEM_FILE_H_

#include <stddef.h>
#include <sys/param.h>

#define SHD_SHMEM_FILE_NAME_NBYTES (NAME_MAX < 256 ? NAME_MAX : 256)

typedef struct _ShMemFile {
    void *p;
    size_t nbytes;
    char name[SHD_SHMEM_FILE_NAME_NBYTES];
} ShMemFile;

int shmemfile_alloc(size_t nbytes, ShMemFile *shmf);
int shmemfile_free(ShMemFile *shmf);

size_t shmemfile_goodSizeNBytes(size_t requested_nbytes);

#endif  // SHD_SHMEM_FILE_H_
