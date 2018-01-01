#ifndef VDL_MEM_H
#define VDL_MEM_H

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// in order to prevent clang from replacing our memcpy and memset definitions
// with a plt lookup, we have to redeclare the c std lib prototypes
void *memcpy(void *d, const void *s, size_t len);
void *memset (void *s, int c, size_t n);
// the compiler will just optimize these to the above,
// we define them for breakpoints specific to elf-loader
void vdl_memcpy (void *d, const void *s, size_t len);
void vdl_memset (void *s, int c, size_t n);

void vdl_memmove (void *dst, const void *src, size_t len);
int vdl_memcmp (void *a, void *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* VDL_MEM_H */
