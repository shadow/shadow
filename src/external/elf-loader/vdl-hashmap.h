#ifndef VDL_HASHMAP_H
#define VDL_HASHMAP_H

#include "vdl-alloc.h"
#include "vdl-list.h"

struct VdlHashMapItem
{
  void *data;
  uint32_t hash;
};

struct VdlHashMap
{
  uint32_t n_buckets;
  unsigned int load;
  // load at which we realloc
  unsigned int max_load;
  struct VdlList **buckets;
};

// "hash" is the hashed form of the key
// "key" is what is given as "query" to "equals"
// "equals" is a function that returns a non-0 value when "query" and "cached" are a match
void *vdl_hashmap_get (struct VdlHashMap *map, uint32_t hash, void *key,
                       int (*equals)(const void *query, const void *cached));
void vdl_hashmap_insert (struct VdlHashMap *map, uint32_t hash, void *data);
struct VdlHashMap *vdl_hashmap_new (void);
void vdl_hashmap_delete(struct VdlHashMap *map);

#endif
