#include "vdl-alloc.h"
#include "alloc.h"

struct Alloc g_alloc;

void vdl_alloc_initialize (void)
{
  alloc_initialize (&g_alloc);
}
void vdl_alloc_destroy (void)
{
  alloc_destroy (&g_alloc);
}

void *vdl_alloc_malloc (size_t size)
{
  return alloc_malloc (&g_alloc, size);
}
void vdl_alloc_free (void *buffer)
{
  if (buffer == 0)
    {
      return;
    }
  return alloc_free (&g_alloc, buffer);
}
