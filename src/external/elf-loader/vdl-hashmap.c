#include "vdl-hashmap.h"
#include "vdl-mem.h"
#include "vdl-log.h"

#define INITIAL_HASHMAP_SIZE 256

static void
grow_hashmap (struct VdlHashMap *map)
{
  uint32_t old_n_buckets = map->n_buckets;
  struct VdlList **old_buckets = map->buckets;
  map->n_buckets = map->n_buckets * 2;
  map->max_load = (3 * map->n_buckets) / 4;
  map->buckets = vdl_alloc_malloc (sizeof (struct VdlList) * map->n_buckets);
  vdl_memset (map->buckets, 0, sizeof (struct VdlList) * map->n_buckets);
  // XXX: make this faster by migrating singleton lists
  uint32_t i;
  for (i = 0; i <= old_n_buckets; i++)
    {
      struct VdlList *bucket = old_buckets[i];
      if (!bucket)
        {
          continue;
        }
      void **cur;
      for (cur = vdl_list_begin (bucket);
           cur != vdl_list_end (bucket); cur = vdl_list_next (cur))
        {
          struct VdlHashMapItem *item = (struct VdlHashMapItem *) (*cur);
          vdl_hashmap_insert (map, item->hash, item->data);
        }
      vdl_list_delete (bucket);
    }
  vdl_alloc_free (old_buckets);
}

void *
vdl_hashmap_get (struct VdlHashMap *map, uint32_t hash, void *key,
                 int (*equals) (const void *query, const void *cached))
{
  struct VdlList *items = map->buckets[hash % map->n_buckets];
  if (!items)
    {
      return 0;
    }
  void **cur;
  for (cur = vdl_list_begin (items);
       cur != vdl_list_end (items); cur = vdl_list_next (cur))
    {
      struct VdlHashMapItem *item = (struct VdlHashMapItem *) (*cur);
      if (hash == item->hash && equals (key, item->data))
        {
          return item->data;
        }
    }
  return 0;
}

void
vdl_hashmap_remove (struct VdlHashMap *map, uint32_t hash, void *key)
{
  struct VdlList *items = map->buckets[hash % map->n_buckets];
  vdl_list_remove (items, key);
  map->load--;
}

void
vdl_hashmap_insert (struct VdlHashMap *map, uint32_t hash, void *data)
{
  if (map->load >= map->max_load)
    {
      grow_hashmap (map);
    }
  struct VdlList *items = map->buckets[hash % map->n_buckets];
  if (!items)
    {
      items = vdl_list_new ();
      map->buckets[hash % map->n_buckets] = items;
    }
  struct VdlHashMapItem *item = vdl_alloc_new (struct VdlHashMapItem);
  item->data = data;
  item->hash = hash;
  vdl_list_push_back (items, item);
  map->load++;
}

struct VdlHashMap *
vdl_hashmap_new (void)
{
  struct VdlHashMap *map = vdl_alloc_new (struct VdlHashMap);
  map->n_buckets = INITIAL_HASHMAP_SIZE;
  map->load = 0;
  map->max_load = INITIAL_HASHMAP_SIZE * 3 / 4;
  map->buckets = vdl_alloc_malloc (sizeof (struct VdlList) * map->n_buckets);
  vdl_memset (map->buckets, 0, sizeof (struct VdlList) * map->n_buckets);
  return map;
}

void
vdl_hashmap_delete (struct VdlHashMap *map)
{
  uint32_t i;
  for (i = 0; i <= map->n_buckets; i++)
    {
      struct VdlList *bucket = map->buckets[i];
      vdl_list_delete (bucket);
    }
  vdl_alloc_free (map->buckets);
  vdl_alloc_delete (map);
}
