#include "vdl-hashmap.h"
#include "vdl-mem.h"
#include "vdl-log.h"

// Because I didn't want to implement a lock-free hash table, this is a lock-
// based hash table I put together. There's probably a better way of doing it
// if someone is willing to take the time.

// this value _must_ be a power of two
#define INITIAL_HASHMAP_SIZE 256

static void
vdl_hashmap_insert_internal (struct VdlHashMap *map, uint32_t hash, void *data)
{
  struct VdlList *items = map->buckets[hash & (map->n_buckets - 1)];
  if (!items)
    {
      items = vdl_list_new ();
      map->buckets[hash & (map->n_buckets - 1)] = items;
    }
  struct VdlHashMapItem *item = vdl_alloc_new (struct VdlHashMapItem);
  item->data = data;
  item->hash = hash;
  vdl_list_push_back (items, item);
  map->load++;
}

static void
grow_hashmap (struct VdlHashMap *map)
{
  write_lock (map->lock);
  if (map->load < map->max_load)
    {
      // map grew before we got the lock
      write_unlock (map->lock);
      return;
    }
  uint32_t old_n_buckets = map->n_buckets;
  struct VdlList **old_buckets = map->buckets;
  map->n_buckets = map->n_buckets * 2;
  map->max_load = (3 * map->n_buckets) / 4;
  map->buckets = vdl_alloc_malloc (sizeof (struct VdlList) * map->n_buckets);
  vdl_memset (map->buckets, 0, sizeof (struct VdlList) * map->n_buckets);
  uint32_t i;
  for (i = 0; i <= old_n_buckets; i++)
    {
      struct VdlList *bucket = old_buckets[i];
      if (!bucket)
        {
          continue;
        }
      // most buckets will have one element, so just point to the old list
      // note: since we're growing to another power of two, and there's no
      // collision in this bucket, there can't be a collision in the new bucket
      // yet, so we don't need to check
      if (bucket->size == 1)
        {
          struct VdlHashMapItem *item =
            (struct VdlHashMapItem *)bucket->head.next->data;
          map->buckets[item->hash & (map->n_buckets - 1)] = bucket;
          continue;
        }
      void **cur;
      for (cur = vdl_list_begin (bucket);
           cur != vdl_list_end (bucket);
           cur = vdl_list_next (bucket, cur))
        {
          struct VdlHashMapItem *item = (struct VdlHashMapItem *) (*cur);
          vdl_hashmap_insert_internal (map, item->hash, item->data);
        }
      vdl_list_delete (bucket);
    }
  write_unlock (map->lock);
  vdl_alloc_free (old_buckets);
}

void *
vdl_hashmap_get (struct VdlHashMap *map, uint32_t hash, void *key,
                 int (*equals) (const void *query, const void *cached))
{
  read_lock(map->lock);
  struct VdlList *items = map->buckets[hash & (map->n_buckets - 1)];
  if (!items)
    {
      read_unlock(map->lock);
      return 0;
    }
  void **cur;
  for (cur = vdl_list_begin (items);
       cur != vdl_list_end (items);
       cur = vdl_list_next (items, cur))
    {
      struct VdlHashMapItem *item = (struct VdlHashMapItem *) (*cur);
      if (hash == item->hash && equals (key, item->data))
        {
          read_unlock(map->lock);
          return item->data;
        }
    }
  read_unlock(map->lock);
  return 0;
}

void
vdl_hashmap_remove (struct VdlHashMap *map, uint32_t hash, void *data)
{
  write_lock (map->lock);
  struct VdlList *items = map->buckets[hash & (map->n_buckets - 1)];
  struct VdlHashMapItem *item;
  void **cur;
  for (cur = vdl_list_begin (items);
       cur != vdl_list_end (items);
       cur = vdl_list_next (items, cur))
    {
      item = (struct VdlHashMapItem *) (*cur);
      if (data == item->data)
        {
          vdl_list_remove (items, item);
          vdl_alloc_delete (item);
          map->load--;
          break;
        }
    }
  write_unlock (map->lock);
}

void
vdl_hashmap_insert (struct VdlHashMap *map, uint32_t hash, void *data)
{
  struct VdlHashMapItem *item = vdl_alloc_new (struct VdlHashMapItem);
  item->data = data;
  item->hash = hash;
  // atomic
  uint32_t load = __sync_fetch_and_add (&map->load, 1);
  read_lock (map->lock);
  if (load >= map->max_load)
    {
      read_unlock (map->lock);
      grow_hashmap (map);
      read_lock (map->lock);
    }
  uint32_t index = hash & (map->n_buckets - 1);
  struct VdlList *items = map->buckets[index];
  if (!items)
    {
      read_unlock (map->lock);
      struct VdlList *new_items = vdl_list_new ();
      write_lock (map->lock);
      if (!items)
        {
          items = new_items;
          map->buckets[index] = items;
          write_unlock (map->lock);
        }
      else
        {
          write_unlock (map->lock);
          vdl_list_delete (new_items);
        }
      read_lock (map->lock);
    }
  vdl_list_push_back (items, item);
  read_unlock (map->lock);
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
  map->lock = rwlock_new();
  return map;
}

void
vdl_hashmap_delete (struct VdlHashMap *map)
{
  uint32_t i;
  for (i = 0; i <= map->n_buckets; i++)
    {
      struct VdlList *bucket = map->buckets[i];
      if(bucket)
        vdl_list_delete (bucket);
    }
  vdl_alloc_free (map->buckets);
  rwlock_delete (map->lock);
  vdl_alloc_delete (map);
}
