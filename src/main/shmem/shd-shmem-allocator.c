#include "shd-shmem-allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "shd-buddy.h"
#include "shd-shmem-file.h"

// #define SHD_SHMEM_ALLOCATOR_POOL_NBYTES SHD_BUDDY_POOL_MAX_NBYTES
#define SHD_SHMEM_ALLOCATOR_POOL_NBYTES 4096

// When to change allocation strategies
#define SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES                                     \
    (SHD_SHMEM_ALLOCATOR_POOL_NBYTES / 2 - sizeof(BuddyControlBlock))

typedef struct _ShMemFileNode {
    struct _ShMemFileNode *prv, *nxt;
    ShMemFile shmf;
} ShMemFileNode;

static ShMemFileNode* _shmemfilenode_findPtr(ShMemFileNode* file_nodes,
                                             uint8_t* p) {

    ShMemFileNode* node = file_nodes;

    // TODO (rwails) fix the possible infinite loop on bad input
    while (true) {

        if (p >= (uint8_t*)node->shmf.p &&
            p < ((uint8_t*)node->shmf.p + node->shmf.nbytes)) {

            printf("FOUND %s\n", node->shmf.name);

            return node;
        }

        node = node->nxt;
    }

    printf("FOUND null\n");
    return NULL;
}

static ShMemFileNode* _shmemfilenode_findName(ShMemFileNode* file_nodes,
                                              const char* name) {

    ShMemFileNode* node = file_nodes;
    bool found = false;

    if (node != NULL) {
        do {
            found = (strcmp(node->shmf.name, name) == 0);
            if (found) { return node; }
            node = node->nxt;
        } while (node != file_nodes);
    }

    if (!found) { return NULL; }
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
};

struct _ShMemSerializer {
    ShMemFileNode* nodes;
};

ShMemAllocator* shmemallocator_create() {
    ShMemAllocator* allocator = calloc(1, sizeof(ShMemAllocator));
    return allocator;
}

void* shmemallocator_destroy(ShMemAllocator* allocator) {
    if (allocator != NULL) {

        ShMemPoolNode* node = allocator->little_alloc_nodes;

        if (node != NULL) {
            do {
                ShMemPoolNode* next_node = (ShMemPoolNode*)node->file_node.nxt;
                _shmempoolnode_destroy(node);
                node = next_node;
            } while (node != allocator->little_alloc_nodes);
        }

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

        _shmemallocator_littleAlloc(allocator, nbytes);
    } else {
        blk.p = p;
        blk.nbytes = nbytes;
        return blk;
    }
}

ShMemBlock shmemallocator_alloc(ShMemAllocator* allocator, size_t nbytes) {
    assert(allocator != NULL);

    if (nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        return _shmemallocator_bigAlloc(allocator, nbytes);
    } else {
        return _shmemallocator_littleAlloc(allocator, nbytes);
    }
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

    if (blk->nbytes > SHD_SHMEM_ALLOCATOR_CUTOVER_NBYTES) {
        _shmemallocator_bigFree(allocator, blk);
    } else {
        _shmemallocator_littleFree(allocator, blk);
    }
}

ShMemBlockSerialized shmemallocator_blockSerialize(ShMemAllocator* allocator,
                                                   ShMemBlock* blk) {
    ShMemBlockSerialized ret;
    ShMemFileNode* node = NULL;

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
    return ret;
}

ShMemBlock shmemallocator_blockDeserialize(ShMemAllocator* allocator,
                                           ShMemBlockSerialized* serial) {
    ShMemBlock ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    ShMemFileNode* node = NULL;
    // scan thru both
    printf("looking %s\n", serial->name);
    node = _shmemfilenode_findName(allocator->big_alloc_nodes, serial->name);

    printf("deserial found node %p %s\n", node, node ? node->shmf.name : 0);

    if (node == NULL) {
        node = _shmemfilenode_findName(
            (ShMemFileNode*)allocator->little_alloc_nodes, serial->name);

        printf("deserial found node %p %s\n", node, node ? node->shmf.name : 0);
    }

    if (node != NULL) {
        ret.p = (uint8_t*)node->shmf.p + serial->offset;
        ret.nbytes = serial->nbytes;
    }

    return ret;
}

ShMemSerializer* shmemserializer_create() {
    return calloc(1, sizeof(ShMemSerializer));
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

        free(serializer);
    }
}

ShMemBlock shmemserializer_blockDeserialize(ShMemSerializer* serializer,
                                            ShMemBlockSerialized* serial) {
    assert(serializer != NULL && serial != NULL);
    ShMemBlock ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    ShMemFileNode* node = NULL;
    node = _shmemfilenode_findName(serializer->nodes, serial->name);

    if (node == NULL) {

        printf("mapping a new block\n");

        ShMemFile shmf;
        int rc = shmemfile_map(serial->name, serial->nbytes, &shmf);
        if (rc != 0) {
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
    return ret;
}

ShMemBlockSerialized shmemserializer_blockSerialize(ShMemSerializer* serializer,
                                                    ShMemBlock* blk)
{
    ShMemBlockSerialized ret;
    memset(&ret, 0, sizeof(ShMemBlock));

    ShMemFileNode* node = NULL;
    node = _shmemfilenode_findPtr(serializer->nodes, blk->p);

    assert(node != NULL);

    ret.nbytes = node->shmf.nbytes;
    ret.offset = (uint8_t*)blk->p - (uint8_t*)node->shmf.p;
    strncpy(ret.name, node->shmf.name, SHD_SHMEM_FILE_NAME_NBYTES);
    return ret;

}
