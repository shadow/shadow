#ifndef VDL_ALLOC_H
#define VDL_ALLOC_H

/**
 * Abstraction layer for handling allocators
 */

#include <unistd.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

void vdl_alloc_initialize (void);
void vdl_alloc_destroy (void);
void *vdl_alloc_allocator (void);

void *vdl_alloc_malloc (size_t size);
void vdl_alloc_free (void *buffer);
#define vdl_alloc_new(type) \
  (type *) vdl_alloc_malloc (sizeof (type))
#define vdl_alloc_delete(v) \
  vdl_alloc_free (v)

// used for managing the allocators, try to avoid using
void *vdl_alloc_global (size_t size);

#ifdef __cplusplus
}
#endif

#endif /* VDL_ALLOC_H */
