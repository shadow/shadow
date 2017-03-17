#include "vdl-alloc.h"
#include "alloc.h"
#include "vdl-tls.h"
#include "vdl.h"

// We want concurrent mallocs and frees, so we give each thread its own
// allocator. This means mallocs happen on their own thread's allocator, while
// free is done on whatever allocator the memory was malloc'ed with.
// But we can't use real thread-local storage to construct an allocator, since
// we're the ones who set up TLS in the first place. So we have two allocators:
// this global one, and the thread-local ones. The global one is used during
// bootstrapping and before TLS is set up on a new thread, while the
// thread-local allocators are used the majority of the run time.
struct Alloc g_alloc;

void
vdl_alloc_initialize (void)
{
  alloc_initialize (&g_alloc);
}

void
vdl_alloc_destroy (void)
{
  alloc_destroy (&g_alloc);
}

void *
vdl_alloc_malloc (size_t size)
{
  struct LocalTLS *local_tls = vdl_tls_get_local_tls ();
  if (local_tls)
    {
      return alloc_malloc (local_tls->allocator, size);
    }
  return alloc_malloc (&g_alloc, size);
}

void
vdl_alloc_free (void *buffer)
{
  if (buffer == 0)
    {
      return;
    }
  return alloc_free (buffer);
}
