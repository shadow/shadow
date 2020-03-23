#ifndef SHD_SHMEM_ALLOCATOR_H_
#define SHD_SHMEM_ALLOCATOR_H_

#include <stddef.h>

#include "shd-shmem-file.h"

typedef struct _ShMemAllocator ShMemAllocator;
typedef struct _ShMemSerializer ShMemSerializer;

typedef struct _ShMemBlock {
    void* p;
    size_t nbytes;
} ShMemBlock;

typedef struct _ShMemBlockSerialized {
    size_t offset;
    size_t nbytes;
    char name[SHD_SHMEM_FILE_NAME_NBYTES];
} ShMemBlockSerialized;

ShMemAllocator* shmemallocator_create();
void shmemallocator_destroy(ShMemAllocator* allocator);

ShMemBlock shmemallocator_alloc(ShMemAllocator* allocator, size_t nbytes);
void shmemallocator_free(ShMemAllocator* allocator, ShMemBlock* blk);

ShMemBlockSerialized
shmemallocator_blockSerialize(const ShMemAllocator* allocator, ShMemBlock* blk);

ShMemBlock shmemallocator_blockDeserialize(const ShMemAllocator* allocator,
                                           ShMemBlockSerialized* serial);

ShMemSerializer* shmemserializer_create();
void shmemserializer_destroy(ShMemSerializer* serializer);

ShMemBlockSerialized
shmemserializer_blockSerialize(const ShMemSerializer* serializer,
                               ShMemBlock* blk);

ShMemBlock shmemserializer_blockDeserialize(ShMemSerializer* serializer,
                                            ShMemBlockSerialized* serial);

#endif // SHD_SHMEM_ALLOCATOR_H_
