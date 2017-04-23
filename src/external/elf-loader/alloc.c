#include "alloc.h"
#include "vdl-mem.h"
#include "system.h"
#include <sys/mman.h>

#ifdef HAVE_VALGRIND_H
#include "valgrind/valgrind.h"
#include "valgrind/memcheck.h"
#define REPORT_MALLOC(buffer, size) \
  VALGRIND_MALLOCLIKE_BLOCK (buffer,size, 0, 0)
#define REPORT_FREE(buffer) \
  VALGRIND_FREELIKE_BLOCK (buffer, 0)
#define MARK_DEFINED(buffer, size) \
  VALGRIND_MAKE_MEM_DEFINED(buffer, size)
#define MARK_UNDEFINED(buffer, size) \
  VALGRIND_MAKE_MEM_UNDEFINED(buffer, size)
#else
#define REPORT_MALLOC(buffer, size)
#define REPORT_FREE(buffer)
#define MARK_DEFINED(buffer, size)
#define MARK_UNDEFINED(buffer, size)
#endif

struct AllocMmapChunk
{
  uint8_t *buffer;
  uint32_t size;
  uint32_t brk;
  struct AllocMmapChunk *next;
};
struct AllocAvailable
{
  struct AllocAvailable *next;
};
struct AllocMallocMetadata
{
  struct Alloc *alloc;
  unsigned long size;
};


static uint32_t
round_to (uint32_t v, uint32_t to)
{
  return (v + (to - (v % to)));
}

static uint32_t
chunk_overhead (void)
{
  return round_to (sizeof (struct AllocMmapChunk), 16);
}

static uint8_t *
alloc_chunk (struct Alloc *alloc, uint32_t size)
{
  size = round_to (size, 4096);
  uint8_t *map = system_mmap (0, size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  struct AllocMmapChunk *chunk = (struct AllocMmapChunk *) (map);
  chunk->buffer = map;
  chunk->size = size;
  chunk->brk = chunk_overhead ();
  chunk->next = alloc->chunks;
  alloc->chunks = chunk;
  MARK_UNDEFINED (chunk->buffer + chunk->brk, size - chunk->brk);
  return map;
}

static uint8_t *
alloc_brk (struct Alloc *alloc, uint32_t needed)
{
  struct AllocMmapChunk *tmp;
  do {
    for (tmp = alloc->chunks; tmp != 0; tmp = tmp->next)
      {
        if (tmp->size - tmp->brk >= needed)
          {
            uint8_t *buffer = tmp->buffer + tmp->brk;
            tmp->brk += needed;
            return buffer;
          }
      }
  } while (alloc_chunk (alloc, alloc->default_mmap_size));
  return 0;
}

static uint8_t
size_to_bucket (uint32_t sz)
{
  uint8_t bucket = 0;
  uint32_t size = sz;
  size--;
  while (size > 7)
    {
      size >>= 1;
      bucket++;
    }
  return bucket;
}

static uint32_t
bucket_to_size (uint8_t bucket)
{
  uint32_t size = (1 << (bucket + 3));
  return size;
}

static void *
alloc_do_malloc (struct Alloc *alloc, uint32_t size)
{
  if (size < (alloc->default_mmap_size - chunk_overhead ()))
    {
      uint8_t bucket = size_to_bucket (size);
      if (alloc->buckets[bucket] != 0)
        {
          // fast path.
          struct AllocAvailable *avail = alloc->buckets[bucket];
          MARK_DEFINED (avail, sizeof (void *));
          struct AllocAvailable *next = avail->next;
          MARK_UNDEFINED (avail, sizeof (void *));
          alloc->buckets[bucket] = next;
          REPORT_MALLOC (avail, size);
          return (uint8_t *) avail;
        }
      // slow path
      struct AllocAvailable *avail = (struct AllocAvailable *)
        alloc_brk (alloc, bucket_to_size (bucket));
      REPORT_MALLOC (avail, size);
      avail->next = 0;
      return (uint8_t *) avail;
    }
  else
    {
      alloc_chunk (alloc, size + chunk_overhead ());
      uint8_t *buffer = alloc_brk (alloc, size);
      REPORT_MALLOC (buffer, size);
      return buffer;
    }
}

static void
alloc_do_free (struct Alloc *alloc, uint8_t * buffer, uint32_t size)
{
  if (size < (alloc->default_mmap_size - chunk_overhead ()))
    {
      // return to bucket list.
      uint8_t bucket = size_to_bucket (size);
      struct AllocAvailable *avail = (struct AllocAvailable *) buffer;
      avail->next = alloc->buckets[bucket];
      alloc->buckets[bucket] = avail;
      REPORT_FREE (buffer);
    }
  else
    {
      struct AllocMmapChunk *tmp, *prev;
      for (tmp = alloc->chunks, prev = 0; tmp != 0;
           prev = tmp, tmp = tmp->next)
        {
          if (tmp->buffer == buffer && tmp->size == size)
            {
              if (prev == 0)
                {
                  alloc->chunks = tmp->next;
                }
              else
                {
                  prev->next = tmp->next;
                }
              REPORT_FREE (buffer);
              system_munmap (tmp->buffer, tmp->size);
              return;
            }
        }
      // this should never happen but it happens in case of a double-free
      REPORT_FREE (buffer);
    }
}

void
alloc_initialize (struct Alloc *alloc)
{
  int i;
  alloc->chunks = 0;
  for (i = 0; i < 32; i++)
    {
      alloc->buckets[i] = 0;
    }
  alloc->default_mmap_size = 1 << 15;
  futex_construct (&alloc->futex);
}

void
alloc_destroy (struct Alloc *alloc)
{
  struct AllocMmapChunk *tmp, *next;
  for (tmp = alloc->chunks; tmp != 0; tmp = next)
    {
      next = tmp->next;
      system_munmap (tmp->buffer, tmp->size);
    }
  alloc->chunks = 0;
  futex_destruct (&alloc->futex);
}

void *
alloc_malloc (struct Alloc *alloc, uint32_t size)
{
  futex_lock (&alloc->futex);
  void *buffer =
    alloc_do_malloc (alloc, size + sizeof (struct AllocMallocMetadata));
  futex_unlock (&alloc->futex);
  struct AllocMallocMetadata *metadata = (struct AllocMallocMetadata *) buffer;
  metadata->alloc = alloc;
  metadata->size = size;
  return buffer + sizeof (struct AllocMallocMetadata);
}

void
alloc_free (void *buffer)
{
  struct AllocMallocMetadata *metadata =
    (struct AllocMallocMetadata *) (buffer - sizeof (struct AllocMallocMetadata));
  unsigned long size = metadata->size;
  struct Alloc *alloc = metadata->alloc;
  //vdl_memset (buf, 0x66, size);
  futex_lock (&alloc->futex);
  alloc_do_free (alloc, (void *) (metadata),
                 size + sizeof (struct AllocMallocMetadata));
  futex_unlock (&alloc->futex);
}
