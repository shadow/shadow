#include "main/shmem/shmem_allocator.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "main/shmem/buddy.h"
#include "main/shmem/shmem_file.h"
#include "main/shmem/shmem_util.h"
#include "support/logger/logger.h"

#define SHD_SHMEM_ALLOCATOR_POOL_NBYTES SHD_BUDDY_POOL_MAX_NBYTES

// when to change allocation strategies
#define SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES                                     \
    (SHD_SHMEM_ALLOCATOR_POOL_NBYTES / 2 - sizeof(BuddyControlBlock))

typedef struct _ShMemFileNode {
    struct _ShMemFileNode *prv, *nxt;
    ShMemFile shmf;
} ShMemFileNode;

static const char *SHMEM_BLOCK_SERIALIZED_STRFMT = "%zu,%zu,%zu,%s";

static const ShMemFileNode*
_shmemfilenode_findPtr(const ShMemFileNode* file_nodes, uint8_t* p) {

    const ShMemFileNode* node = file_nodes;

    if (node) {
        do {
            if (p >= (uint8_t*)node->shmf.p &&
                p < ((uint8_t*)node->shmf.p + node->shmf.nbytes)) {
                return node;
            }

            node = node->nxt;
        } while (node != file_nodes);
    }

    return NULL;
}

static const ShMemFileNode*
_shmemfilenode_findName(const ShMemFileNode* file_nodes, const char* name) {

    const ShMemFileNode* node = file_nodes;

    if (node) {
        do {
            if (strcmp(node->shmf.name, name) == 0) {
                return node;
            }
            node = node->nxt;
        } while (node != file_nodes);
    }

    return NULL;
}

typedef struct _ShMemPoolNode {
    ShMemFileNode file_node;
    uint8_t meta[SHD_BUDDY_META_MAX_NBYTES];
} ShMemPoolNode;

static ShMemPoolNode* _shmempoolnode_create() {

    ShMemFile shmf;
    int rc = shmemfile_alloc(SHD_SHMEM_ALLOCATOR_POOL_NBYTES, &shmf);

    if (rc == -1) {
        return NULL;
    }

    ShMemPoolNode* ret = calloc(1, sizeof(ShMemPoolNode));

    if (ret) {
        ret->file_node.nxt = (ShMemFileNode*)ret;
        ret->file_node.prv = (ShMemFileNode*)ret;
        ret->file_node.shmf = shmf;

        buddy_poolInit(ret->file_node.shmf.p, SHD_SHMEM_ALLOCATOR_POOL_NBYTES);
        buddy_metaInit(
            ret->meta, ret->file_node.shmf.p, SHD_SHMEM_ALLOCATOR_POOL_NBYTES);

        return ret;
    } else {
        shmemfile_free(&shmf);
        return NULL;
    }
}

static void _shmempoolnode_destroy(ShMemPoolNode* node) {
    if (node) {
        shmemfile_free(&node->file_node.shmf);
        free(node);
    }
}

struct _ShMemAllocator {
    ShMemFileNode* big_alloc_nodes;
    ShMemPoolNode* little_alloc_nodes;
    pthread_mutex_t mtx;
};

struct _ShMemSerializer {
    ShMemFileNode* nodes;
    pthread_mutex_t mtx;
};

static ShMemAllocator* _global_allocator = NULL;
static ShMemSerializer* _global_serializer = NULL;

/*
 * hook used to cleanup at exit.
 */
static void _shmemallocator_destroyGlobal() {
    assert(_global_allocator);
    shmemallocator_destroy(_global_allocator);
}

/*
 * hook used to cleanup at exit.
 */
static void _shmemserializer_destroyGlobal() {
    assert(_global_serializer);
    shmemserializer_destroy(_global_serializer);
}

ShMemAllocator* shmemallocator_getGlobal() {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mtx);

    if (!_global_allocator) { // need to initialize
        _global_allocator = shmemallocator_create();

        if (_global_allocator) { // set up hooks for free on exit
            atexit(_shmemallocator_destroyGlobal);
        } else { // something bad happened, and we definitely can't continue
            error("error allocating global shared memory allocator");
            abort();
        }
    }

    pthread_mutex_unlock(&mtx);
    return _global_allocator;
}

ShMemSerializer* shmemserializer_getGlobal() {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mtx);

    if (!_global_serializer) { // need to initialize
        _global_serializer = shmemserializer_create();

        if (_global_serializer) { // set up hooks for free on exit
            atexit(_shmemserializer_destroyGlobal);
        } else { // something bad happened, and we definitely can't continue
            error("error allocating global shared memory serializer");
            abort();
        }
    }

    pthread_mutex_unlock(&mtx);
    return _global_serializer;
}

ShMemAllocator* shmemallocator_create() {
    ShMemAllocator* allocator = calloc(1, sizeof(ShMemAllocator));

    if (allocator) {
        pthread_mutex_init(&allocator->mtx, NULL);
    }

    return allocator;
}

void shmemallocator_destroy(ShMemAllocator* allocator) {
    assert(allocator);

    ShMemPoolNode* node = allocator->little_alloc_nodes;

    if (node) {
        do {
            ShMemPoolNode* next_node = (ShMemPoolNode*)node->file_node.nxt;
            _shmempoolnode_destroy(node);
            node = next_node;
        } while (node != allocator->little_alloc_nodes);
    }

    pthread_mutex_destroy(&allocator->mtx);

    free(allocator);
}

void shmemallocator_destroyNoShmDelete(ShMemAllocator* allocator) {
    assert(allocator);

    ShMemPoolNode* node = allocator->little_alloc_nodes;

    if (node != NULL) {
        do {
            ShMemPoolNode* next_node = (ShMemPoolNode*)node->file_node.nxt;
            free(next_node);
            node = next_node;
        } while (node != allocator->little_alloc_nodes);
    }

    pthread_mutex_destroy(&allocator->mtx);

    free(allocator);
}

static ShMemBlock _shmemallocator_bigAlloc(ShMemAllocator* allocator,
                                           size_t nbytes) {
    ShMemBlock blk;
    memset(&blk, 0, sizeof(ShMemBlock));

    size_t good_size_nbytes = shmemfile_goodSizeNBytes(nbytes);
    ShMemFile shmf;
    int rc = shmemfile_alloc(good_size_nbytes, &shmf);

    if (rc == 0) {
        ShMemFileNode* file_node = calloc(1, sizeof(ShMemFileNode));

        if (file_node) {
            blk.p = shmf.p;
            blk.nbytes = nbytes;

            // create a new node to track
            file_node->shmf = shmf;

            if (allocator->big_alloc_nodes == NULL) {
                file_node->prv = file_node;
                file_node->nxt = file_node;
                allocator->big_alloc_nodes = file_node;
            } else {
                ShMemFileNode* last = allocator->big_alloc_nodes->prv;
                last->nxt = file_node;
                file_node->prv = last;
                file_node->nxt = allocator->big_alloc_nodes;
                allocator->big_alloc_nodes->prv = file_node;
            }
        }
    }

    return blk;
}

static ShMemBlock _shmemallocator_littleAlloc(ShMemAllocator* allocator,
                                              size_t nbytes) {

    ShMemBlock blk;
    memset(&blk, 0, sizeof(ShMemBlock));

    if (allocator->little_alloc_nodes == NULL) {
        allocator->little_alloc_nodes = _shmempoolnode_create();
    }

    ShMemPoolNode* pool_node = allocator->little_alloc_nodes;
    void* p = NULL;

    do { // try to make the alloc in the pool

        p = buddy_alloc(nbytes, pool_node->meta, pool_node->file_node.shmf.p,
                        SHD_SHMEM_ALLOCATOR_POOL_NBYTES);

    } while (pool_node != allocator->little_alloc_nodes && p == NULL);

    if (p == NULL) {
        // If we couldn't make an allocation, create a new pool and try again.
        ShMemFileNode* new_head = (ShMemFileNode*)_shmempoolnode_create();

        if (new_head == NULL) {
            return blk;
        }

        ShMemFileNode* old_head = (ShMemFileNode*)allocator->little_alloc_nodes;

        old_head->prv->nxt = new_head;
        new_head->prv = old_head->prv;
        new_head->nxt = old_head;
        old_head->prv = new_head;

        allocator->little_alloc_nodes = (ShMemPoolNode*)new_head;

        return _shmemallocator_littleAlloc(allocator, nbytes);
    } else {
        blk.p = p;
        blk.nbytes = nbytes;
        return blk;
    }
}

ShMemBlock shmemallocator_alloc(ShMemAllocator* allocator, size_t nbytes) {
    assert(allocator);

    ShMemBlock blk;
    memset(&blk, 0, sizeof(ShMemBlock));

    if (nbytes == 0) {
        return blk;
    }

    pthread_mutex_lock(&allocator->mtx);

    if (nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        blk = _shmemallocator_bigAlloc(allocator, nbytes);
    } else {
        blk = _shmemallocator_littleAlloc(allocator, nbytes);
    }

    pthread_mutex_unlock(&allocator->mtx);

    return blk;
}

static void _shmemallocator_bigFree(ShMemAllocator* allocator,
                                    ShMemBlock* blk) {
    assert(allocator->big_alloc_nodes);

    // find the block to delete
    ShMemFileNode* needle = allocator->big_alloc_nodes;

    do {
        if (needle->shmf.p == blk->p) {
            break;
        }
        needle = needle->nxt;
    } while (needle != allocator->big_alloc_nodes);

    assert(needle->shmf.p == blk->p);

    if (allocator->big_alloc_nodes == needle) { // if the needle is the head
        if (needle->nxt == needle) { // if the needle is the only node
            allocator->big_alloc_nodes = NULL;
        } else {
            allocator->big_alloc_nodes = needle->nxt;
        }
    }

    needle->prv->nxt = needle->nxt;
    needle->nxt->prv = needle->prv;
    shmemfile_free(&needle->shmf);
    free(needle);
}

static void _shmemallocator_littleFree(ShMemAllocator* allocator,
                                       ShMemBlock* blk) {
    ShMemPoolNode* pool_node = (ShMemPoolNode*)_shmemfilenode_findPtr(
        (ShMemFileNode*)allocator->little_alloc_nodes, blk->p);

    assert(pool_node);

    buddy_free(blk->p, pool_node->meta, pool_node->file_node.shmf.p,
               SHD_SHMEM_ALLOCATOR_POOL_NBYTES);
}

void shmemallocator_free(ShMemAllocator* allocator, ShMemBlock* blk) {
    assert(allocator && blk);

    pthread_mutex_lock(&allocator->mtx);

    if (blk->nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        _shmemallocator_bigFree(allocator, blk);
    } else {
        _shmemallocator_littleFree(allocator, blk);
    }

    pthread_mutex_unlock(&allocator->mtx);
}

static void _shmemblockserialized_populate(const ShMemBlock* blk,
                                           const ShMemFile* shmf,
                                           ShMemBlockSerialized* serial) {
    serial->nbytes = shmf->nbytes;
    serial->offset = (const uint8_t*)blk->p - (const uint8_t*)shmf->p;
    serial->block_nbytes = blk->nbytes;
    strncpy(serial->name, shmf->name, SHD_SHMEM_FILE_NAME_NBYTES);
}

ShMemBlockSerialized shmemallocator_blockSerialize(ShMemAllocator* allocator,
                                                   ShMemBlock* blk) {
    assert(allocator && blk);

    ShMemBlockSerialized ret;
    memset(&ret, 0, sizeof(ShMemBlockSerialized));

    const ShMemFileNode* node = NULL;

    pthread_mutex_lock(&allocator->mtx);

    if (blk->nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        node = _shmemfilenode_findPtr(allocator->big_alloc_nodes, blk->p);
    } else {
        node = _shmemfilenode_findPtr(
            (ShMemFileNode*)allocator->little_alloc_nodes, blk->p);
    }

    assert(node);

    _shmemblockserialized_populate(blk, &node->shmf, &ret);

    pthread_mutex_unlock(&allocator->mtx);
    return ret;
}

static void _shmemblock_populate(const ShMemBlockSerialized* serial,
                                 const ShMemFile* shmf, ShMemBlock* blk) {
    blk->p = (uint8_t*)shmf->p + serial->offset;
    blk->nbytes = serial->block_nbytes;
}

ShMemBlock shmemallocator_blockDeserialize(ShMemAllocator* allocator,
                                           ShMemBlockSerialized* serial) {
    assert(allocator && serial);

    ShMemBlock ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    const ShMemFileNode* node = NULL;
    pthread_mutex_lock(&allocator->mtx);

    // scan thru both
    node = _shmemfilenode_findName(allocator->big_alloc_nodes, serial->name);

    if (!node) {
        node = _shmemfilenode_findName(
            (ShMemFileNode*)allocator->little_alloc_nodes, serial->name);
    }

    assert(node);

    _shmemblock_populate(serial, &node->shmf, &ret);

    pthread_mutex_unlock(&allocator->mtx);
    return ret;
}

ShMemSerializer* shmemserializer_create() {
    ShMemSerializer* serializer = calloc(1, sizeof(ShMemSerializer));

    if (serializer) {
        pthread_mutex_init(&serializer->mtx, NULL);
    }

    return serializer;
}

void shmemserializer_destroy(ShMemSerializer* serializer) {
    assert(serializer);

    ShMemFileNode* node = serializer->nodes;

    if (node) {
        do {
            ShMemFileNode* next_node = node->nxt;
            int rc = shmemfile_unmap(&node->shmf);
            assert(rc == 0);
            free(node);
            node = next_node;
        } while (node != serializer->nodes);
    }

    pthread_mutex_destroy(&serializer->mtx);
    free(serializer);
}

ShMemBlockSerialized shmemserializer_blockSerialize(ShMemSerializer* serializer,
                                                    ShMemBlock* blk) {
    assert(serializer && blk);

    ShMemBlockSerialized ret;
    memset(&ret, 0, sizeof(ShMemBlockSerialized));

    const ShMemFileNode* node = NULL;

    pthread_mutex_lock(&serializer->mtx);
    node = _shmemfilenode_findPtr(serializer->nodes, blk->p);

    assert(node);

    _shmemblockserialized_populate(blk, &node->shmf, &ret);

    pthread_mutex_unlock(&serializer->mtx);
    return ret;
}

ShMemBlock
shmemserializer_blockDeserialize(ShMemSerializer* serializer,
                                 const ShMemBlockSerialized* serial) {
    assert(serializer && serial);
    ShMemBlock ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    const ShMemFileNode* node = NULL;

    pthread_mutex_lock(&serializer->mtx);

    node = _shmemfilenode_findName(serializer->nodes, serial->name);

    if (!node) {

        ShMemFile shmf;
        int rc = shmemfile_map(serial->name, serial->nbytes, &shmf);
        if (rc != 0) {
            // scary!
            pthread_mutex_unlock(&serializer->mtx);
            return ret;
        }

        // we are missing that node, so let's map it in.
        ShMemFileNode* new_node = calloc(1, sizeof(ShMemFileNode));
        new_node->shmf = shmf;

        if (serializer->nodes == NULL) {
            new_node->prv = new_node;
            new_node->nxt = new_node;
            serializer->nodes = new_node;
        } else { // put it at the end

            ShMemFileNode* old_head = serializer->nodes;
            old_head->prv->nxt = new_node;
            new_node->prv = old_head->prv;
            new_node->nxt = old_head;
            old_head->prv = new_node;
        }

        node = new_node;
    }

    _shmemblock_populate(serial, &node->shmf, &ret);

    pthread_mutex_unlock(&serializer->mtx);
    return ret;
}


void shmemblockserialized_toString(const ShMemBlockSerialized *serial,
                                   char *out)
{
    assert(serial && out);

    sprintf(out, SHMEM_BLOCK_SERIALIZED_STRFMT,
            serial->offset,
            serial->nbytes,
            serial->block_nbytes,
            serial->name);
}

ShMemBlockSerialized shmemblockserialized_fromString(const char *buf,
                                                     bool *err)
{
    ShMemBlockSerialized rv = {0};

    assert(buf);

    int rc = sscanf(buf, SHMEM_BLOCK_SERIALIZED_STRFMT,
                    &rv.offset,
                    &rv.nbytes,
                    &rv.block_nbytes,
                    &rv.name);

    if (err) {
        *err = (rc != 4);
    }

    return rv;
}
