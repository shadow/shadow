#ifndef SHD_SHMEM_ALLOCATOR_H_
#define SHD_SHMEM_ALLOCATOR_H_

/*
 * Public API that exposes a shared-memory allocator and ``serializer''.
 * The allocator is intended to be a singleton and held by phantom.  The
 * serializer implements functionality to map/unmap blocks of shared-memory
 * into the process's space, but doesn't implement alloc/free functions.  Each
 * plugin process will probably hold a serializer.
 */

#include <stddef.h>

#include "shd-shmem-file.h"

typedef struct _ShMemAllocator ShMemAllocator;
typedef struct _ShMemSerializer ShMemSerializer;

typedef struct _ShMemBlock {
    void* p;       // pointer to the allocator memory
    size_t nbytes; // size of the allocation
} ShMemBlock;

typedef struct _ShMemBlockSerialized {
    size_t offset;       // offset of the file within the shared memory file
    size_t nbytes;       // size of the shared memory file
    size_t block_nbytes; // size of the block within the file
    char name[SHD_SHMEM_FILE_NAME_NBYTES]; // name of the shared memory file
} ShMemBlockSerialized;

/*
 * Returns a pointer to a pre-initialized, process-global shared-memory
 * allocator.  This object is owned by the process: the caller should not call
 * free or destroy.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel.
 *
 * POST: returns a pointer to the initialized global allocator or aborts if the
 * global serializer could not be created.
 */
ShMemAllocator* shmemallocator_getGlobal();

/*
 * Returns a pointer to a pre-initialized, process-global shared-memory
 * serializer.  This object is owned by the process: the caller should not call
 * free or destroy.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel.
 *
 * POST: returns a pointer to the initialized global serializer or aborts if the
 * global serializer could not be created.
 */
ShMemSerializer* shmemserializer_getGlobal();

/*
 * Heap-allocate and initialize a shared-memory allocator.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel.
 *
 * POST: returns a pointer to an initialized allocator or null if an error
 * (e.g. out-of-memory) occurs.
 */
ShMemAllocator* shmemallocator_create();

/*
 * Reclaim resources associated w/ and deallocate a shared-memory allocator.
 * Does *not* necessarily free all memory back to the OS -- this object will
 * leak memory if free hasn't been called on all allocated blocks.  The
 * allocator is invalidated.
 *
 * THREAD SAFETY: not thread-safe; should only be called once per allocator
 * object.  OK to call in parallel on separate allocator objects.
 *
 * PRE: allocator is non-null and points to an valid allocator created by
 * shmemallocator_create().
 *
 * POST: the allocator will be destroyed and invalidated.
 */
void shmemallocator_destroy(ShMemAllocator* allocator);

/*
 * Reclaim resources associated w/ and deallocate a shared-memory allocator.
 * Does not delete any shared memory pages that were automatically acquired by
 * this shared memory allocator.
 *
 * THREAD SAFETY: not thread-safe; should only be called once per allocator
 * object.  OK to call in parallel on separate allocator objects.
 *
 * PRE: allocator is non-null and points to an valid allocator created by
 * shmemallocator_create().
 *
 * POST: the allocator will be destroyed and invalidated.
 */
void shmemallocator_destroyNoShmDelete(ShMemAllocator* allocator);

/*
 * Semantically similar to malloc(nbytes), except the memory allocated will
 * live in shared memory.  The allocator will try to fit the request into
 * shared-memory pages that are already mapped into this process's address
 * space.  If the allocation will not fit, a new page will be created and
 * mapped.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel on the
 * same allocator object.
 *
 * PRE: allocator is non-null and points to a valid allocator created by
 * shmemallocator_create().  Requires nbytes > 0.
 *
 * POST: if the allocation request could be fulfilled, then returns a valid
 * block with a non-null pointer and with an nbytes field that corresponds to
 * the size requested.  Otherwise, returns an invalid block with
 * blk.p == NULL and blk.nbytes == 0.
 */
ShMemBlock shmemallocator_alloc(ShMemAllocator* allocator, size_t nbytes);

static inline ShMemBlock shmemallocator_globalAlloc(size_t nbytes) {
    return shmemallocator_alloc(shmemallocator_getGlobal(), nbytes);
}

/*
 * Semantically similar to free(blk->p).  Returns the shared-memory to the
 * allocator and invalidates the block.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel on the
 * same allocator object (should only be called once per block, however).
 *
 * PRE: allocator is non-null and points to a valid allocator created by
 * shmemallocator_create().  blk is non-null and points to a valid block
 * allocated by the argument allocator object.
 *
 * POST: the block is destroyed and invalidated.
 */
void shmemallocator_free(ShMemAllocator* allocator, ShMemBlock* blk);

static inline void shmemallocator_globalFree(ShMemBlock* blk) {
    return shmemallocator_free(shmemallocator_getGlobal(), blk);
}

/*
 * Converts a ShMemBlock created by an allocator into a format that is
 * appropriate to cross a process boundary (i.e. ShMemBlockSerialized).
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel on the
 * same allocator object.
 *
 * PRE: allocator is non-null and points to a valid allocator created by
 * shmemallocator_create().  blk is non-null and points to a valid block
 * allocated by the argument allocator object.
 *
 * POST: returns a valid ShMemBlockSerialized that can be deserialized (by the
 * same process, or, a different process) to retrieve a ShMemBlock that points
 * to the same memory as the input block.
 */
ShMemBlockSerialized shmemallocator_blockSerialize(ShMemAllocator* allocator,
                                                   ShMemBlock* blk);

static inline ShMemBlockSerialized
shmemallocator_globalBlockSerialize(ShMemBlock* blk) {
    return shmemallocator_blockSerialize(shmemallocator_getGlobal(), blk);
}

/*
 * Converts a valid ShMemBlockSerialized to a valid ShMemBlock.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel on the
 * same allocator object.
 *
 * PRE: allocator is non-null and points to a valid allocator created by
 * shmemallocator_create().  serial is non-null and points to a valid
 * ShMemBlockSerialized.  NOTE: The original block that was serialized must
 * belong to the argument allocator for this conversion to work.
 *
 * POST: returns a valid ShMemBlock that corresponds to the serialized block.
 * It should be the case that, for any valid block blk,
 *
 *      blockDeserialize(blockSerialize(blk)) == blk
 */
ShMemBlock shmemallocator_blockDeserialize(ShMemAllocator* allocator,
                                           ShMemBlockSerialized* serial);

static inline ShMemBlock
shmemallocator_globalBlockDeserialize(ShMemBlockSerialized* serial) {
    return shmemallocator_blockDeserialize(shmemallocator_getGlobal(), serial);
}

/*
 * Heap-allocate and initialize a shared-memory serializer.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel.
 *
 * POST: returns a pointer to an initialized serializer or null if an error
 * (e.g. out-of-memory) occurs.
 */
ShMemSerializer* shmemserializer_create();

/*
 * Reclaim resources associated w/ and deallocate a shared-memory serializer.
 *
 * THREAD SAFETY: not thread-safe; should only be called once per serializer
 * object.  OK to call in parallel on separate serializer objects.
 *
 * PRE: serializer is non-null and points to an initialized serializer created
 * by shmemserializer_create().
 *
 * POST: the serialzer will be destroyed and invalidated.
 */
void shmemserializer_destroy(ShMemSerializer* serializer);

/*
 * Converts a ShMemBlock *created by this serializer* into a format that is
 * appropriate to cross a process boundary (i.e. ShMemBlockSerialized).
 * Seems unlikely that this function will be used often, but might come in
 * handy.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel on the
 * same serializer object.
 *
 * PRE: serializer is non-null and points to a valid serializer created by
 * shmemserializer_create().  blk is non-null and points to a valid block
 * deserialized by the argument serializer object.
 *
 * POST: returns a valid ShMemBlockSerialized that is identical to the
 * serialized block that was used to generate the input ShMemBlock.
 */
ShMemBlockSerialized shmemserializer_blockSerialize(ShMemSerializer* serializer,
                                                    ShMemBlock* blk);

static inline ShMemBlockSerialized
shmemserializer_globalBlockSerialize(ShMemBlock* blk) {
    return shmemserializer_blockSerialize(shmemserializer_getGlobal(), blk);
}

/*
 * Converts a valid ShMemBlockSerialized to a valid ShMemBlock.  This operation
 * may map a shared-memory file into process memory, if necessary.
 *
 * THREAD SAFETY: thread-safe; can be called by two threads in parallel on the
 * same serializer object.
 *
 * PRE: serializer is non-null and points to a valid allocator created by
 * shmemserializer_create().  serial is non-null and points to a valid
 * ShMemBlockSerialized created by *some* allocator.
 */
ShMemBlock shmemserializer_blockDeserialize(ShMemSerializer* serializer,
                                            ShMemBlockSerialized* serial);

static inline ShMemBlock
shmemserializer_globalBlockDeserialize(ShMemBlockSerialized* serial) {
    return shmemserializer_blockDeserialize(
        shmemserializer_getGlobal(), serial);
}

#endif // SHD_SHMEM_ALLOCATOR_H_
