#ifndef ALLOC_H
#define ALLOC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AllocMmapChunk;
struct AllocAvailable;

struct Alloc
{
  struct AllocMmapChunk *chunks;
  struct AllocAvailable *buckets[32];
  uint32_t default_mmap_size;
};

void alloc_initialize (struct Alloc *alloc);
void alloc_destroy (struct Alloc *alloc);
uint8_t *alloc_malloc (struct Alloc *alloc, uint32_t size);
void alloc_free (struct Alloc *alloc, uint8_t *buffer);

#ifdef __cplusplus
}
#endif

#endif /* ALLOC_H */
