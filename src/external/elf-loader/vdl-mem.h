#ifndef VDL_MEM_H
#define VDL_MEM_H

#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

void vdl_memcpy (void *dst, const void *src, size_t len);
void vdl_memmove (void *dst, const void *src, size_t len);
void vdl_memset(void *s, int c, size_t n);
int vdl_memcmp (void *a, void *b, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* VDL_MEM_H */
