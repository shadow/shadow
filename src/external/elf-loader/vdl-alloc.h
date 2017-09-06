#ifndef VDL_ALLOC_H
#define VDL_ALLOC_H

/**
 * A thin wrapper around the global variable which holds the 
 * allocator state.
 */

#include <unistd.h> // for size_t

#ifdef __cplusplus
extern "C" {
#endif

void vdl_alloc_initialize (void);
void vdl_alloc_destroy (void);

void *vdl_alloc_malloc (size_t size);
void vdl_alloc_free (void *buffer);
#define vdl_alloc_new(type) \
  (type *) vdl_alloc_malloc (sizeof (type))
#define vdl_alloc_delete(v) \
  vdl_alloc_free (v)

#ifdef __cplusplus
}
#endif

#endif /* VDL_ALLOC_H */
