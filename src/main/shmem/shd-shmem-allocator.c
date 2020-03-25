#include "shd-shmem-allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "shd-buddy.h"
#include "shd-shmem-file.h"

#define SHD_SHMEM_ALLOCATOR_POOL_NBYTES SHD_BUDDY_POOL_MAX_NBYTES

// When to change allocation strategies
#define SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES                                     \
    (SHD_SHMEM_ALLOCATOR_POOL_NBYTES / 2 - sizeof(BuddyControlBlock))

typedef struct _ShMemFileNode {
    struct _ShMemFileNode *prv, *nxt;
    ShMemFile shmf;
} ShMemFileNode;

static const ShMemFileNode*
_shmemfilenode_findPtr(const ShMemFileNode* file_nodes, uint8_t* p) {

    const ShMemFileNode* node = file_nodes;

    // TODO (rwails) fix the possible infinite loop on bad input
    while (true) {

        if (p >= (uint8_t*)node->shmf.p &&
            p < ((uint8_t*)node->shmf.p + node->shmf.nbytes)) {

            return node;
        }

        node = node->nxt;
    }

    return NULL;
}

static const ShMemFileNode*
_shmemfilenode_findName(const ShMemFileNode* file_nodes, const char* name) {

    const ShMemFileNode* node = file_nodes;
    bool found = false;

    if (node != NULL) {
        do {
            found = (strcmp(node->shmf.name, name) == 0);
            if (found) {
                return node;
            }
            node = node->nxt;
        } while (node != file_nodes);
    }

    assert(!found);
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
    ret->file_node.nxt = (ShMemFileNode*)ret;
    ret->file_node.prv = (ShMemFileNode*)ret;
    ret->file_node.shmf = shmf;

    buddy_poolInit(ret->file_node.shmf.p, SHD_SHMEM_ALLOCATOR_POOL_NBYTES);
    buddy_metaInit(ret->meta, ret->file_node.shmf.p,
                   SHD_SHMEM_ALLOCATOR_POOL_NBYTES);

    return ret;
}

static void _shmempoolnode_destroy(ShMemPoolNode* node) {
    if (node != NULL) {
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

ShMemAllocator* shmemallocator_create() {
    ShMemAllocator* allocator = calloc(1, sizeof(ShMemAllocator));

    if (allocator != NULL) {
        pthread_mutex_init(&allocator->mtx, NULL);
    }

    return allocator;
}

void shmemallocator_destroy(ShMemAllocator* allocator) {
    if (allocator != NULL) {

        ShMemPoolNode* node = allocator->little_alloc_nodes;

        if (node != NULL) {
            do {
                ShMemPoolNode* next_node = (ShMemPoolNode*)node->file_node.nxt;
                _shmempoolnode_destroy(node);
                node = next_node;
            } while (node != allocator->little_alloc_nodes);
        }

        pthread_mutex_destroy(&allocator->mtx);

        free(allocator);
    }
}

static ShMemBlock _shmemallocator_bigAlloc(ShMemAllocator* allocator,
                                           size_t nbytes) {
    ShMemBlock blk;
    memset(&blk, 0, sizeof(ShMemBlock));

    size_t good_size_nbytes = shmemfile_goodSizeNBytes(nbytes);
    ShMemFile shmf;
    int rc = shmemfile_alloc(good_size_nbytes, &shmf);

    if (rc == 0) {
        blk.p = shmf.p;
        blk.nbytes = nbytes;

        // create a new node to track
        ShMemFileNode* file_node = calloc(1, sizeof(ShMemFileNode));
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
    assert(allocator != NULL);

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
    assert(allocator->big_alloc_nodes != NULL);

    // find the block to delete
    ShMemFileNode* needle = allocator->big_alloc_nodes;

    // TODO (rwails) : fix the possible infinite loop on bad input.
    while (needle->shmf.p != blk->p) {
        needle = needle->nxt;
    }

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

    assert(pool_node != NULL);

    buddy_free(blk->p, pool_node->meta, pool_node->file_node.shmf.p,
               SHD_SHMEM_ALLOCATOR_POOL_NBYTES);
}

void shmemallocator_free(ShMemAllocator* allocator, ShMemBlock* blk) {
    assert(allocator != NULL && blk != NULL);

    pthread_mutex_lock(&allocator->mtx);

    if (blk->nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        _shmemallocator_bigFree(allocator, blk);
    } else {
        _shmemallocator_littleFree(allocator, blk);
    }

    pthread_mutex_unlock(&allocator->mtx);
}

ShMemBlockSerialized
shmemallocator_blockSerialize(const ShMemAllocator* allocator,
                              ShMemBlock* blk) {
    ShMemBlockSerialized ret;
    const ShMemFileNode* node = NULL;

    pthread_mutex_lock(&((ShMemAllocator*)allocator)->mtx);

    if (blk->nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        node = _shmemfilenode_findPtr(allocator->big_alloc_nodes, blk->p);
    } else {
        node = _shmemfilenode_findPtr(
            (ShMemFileNode*)allocator->little_alloc_nodes, blk->p);
    }

    assert(node != NULL);
    ret.nbytes = node->shmf.nbytes;
    ret.offset = (uint8_t*)blk->p - (uint8_t*)node->shmf.p;
    strncpy(ret.name, node->shmf.name, SHD_SHMEM_FILE_NAME_NBYTES);

    pthread_mutex_unlock(&((ShMemAllocator*)allocator)->mtx);
    return ret;
}

ShMemBlock shmemallocator_blockDeserialize(const ShMemAllocator* allocator,
                                           ShMemBlockSerialized* serial) {
    ShMemBlock ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    const ShMemFileNode* node = NULL;
    pthread_mutex_lock(&((ShMemAllocator*)allocator)->mtx);

    // scan thru both
    node = _shmemfilenode_findName(allocator->big_alloc_nodes, serial->name);

    if (node == NULL) {
        node = _shmemfilenode_findName(
            (ShMemFileNode*)allocator->little_alloc_nodes, serial->name);
    }

    if (node != NULL) {
        ret.p = (uint8_t*)node->shmf.p + serial->offset;
        ret.nbytes = serial->nbytes;
    }

    pthread_mutex_unlock(&((ShMemAllocator*)allocator)->mtx);
    return ret;
}

ShMemSerializer* shmemserializer_create() {
    ShMemSerializer* serializer = calloc(1, sizeof(ShMemSerializer));
    pthread_mutex_init(&serializer->mtx, NULL);
    return serializer;
}

void shmemserializer_destroy(ShMemSerializer* serializer) {
    if (serializer != NULL) {

        ShMemFileNode* node = serializer->nodes;

        if (node != NULL) {
            do {
                ShMemFileNode* next_node = node->nxt;
                free(node);
                node = next_node;
            } while (node != serializer->nodes);
        }

        pthread_mutex_destroy(&serializer->mtx);
        free(serializer);
    }
}

ShMemBlock shmemserializer_blockDeserialize(ShMemSerializer* serializer,
                                            ShMemBlockSerialized* serial) {
    assert(serializer != NULL && serial != NULL);
    ShMemBlock ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    const ShMemFileNode* node = NULL;

    pthread_mutex_lock(&serializer->mtx);

    node = _shmemfilenode_findName(serializer->nodes, serial->name);

    if (node == NULL) {

        ShMemFile shmf;
        int rc = shmemfile_map(serial->name, serial->nbytes, &shmf);
        if (rc != 0) {
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

    ret.p = node->shmf.p + serial->offset;
    ret.nbytes = serial->nbytes;

    pthread_mutex_unlock(&serializer->mtx);
    return ret;
}

ShMemBlockSerialized
shmemserializer_blockSerialize(const ShMemSerializer* serializer,
                               ShMemBlock* blk) {
    ShMemBlockSerialized ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    const ShMemFileNode* node = NULL;

    pthread_mutex_lock(&((ShMemSerializer *)serializer)->mtx);
    node = _shmemfilenode_findPtr(serializer->nodes, blk->p);

    assert(node != NULL);

    ret.nbytes = node->shmf.nbytes;
    ret.offset = (uint8_t*)blk->p - (uint8_t*)node->shmf.p;
    strncpy(ret.name, node->shmf.name, SHD_SHMEM_FILE_NAME_NBYTES);

    pthread_mutex_unlock(&((ShMemSerializer *)serializer)->mtx);
    return ret;
}
