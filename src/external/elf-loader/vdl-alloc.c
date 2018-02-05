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
  g_vdl.allocators = vdl_list_new ();
}

void
vdl_alloc_destroy_and_free (void *v_alloc)
{
  struct Alloc *alloc = v_alloc;
  alloc_destroy (alloc);
  vdl_alloc_free (alloc);
}

void
vdl_alloc_destroy (void)
{
  vdl_list_iterate (g_vdl.allocators, vdl_alloc_destroy_and_free);
  vdl_list_delete (g_vdl.allocators);
  alloc_destroy (&g_alloc);
}

void *
vdl_alloc_global (size_t size)
{
  return alloc_malloc(&g_alloc, size);
}

void *
vdl_alloc_allocator (void)
{
  // all allocators need to be allocated from the global allocator,
  // so we can safely free them all at the end
  struct Alloc *allocator = alloc_malloc(&g_alloc, sizeof(struct Alloc));
  alloc_initialize (allocator);
  vdl_list_global_push_back (g_vdl.allocators, allocator);
  return allocator;
}

void *
vdl_alloc_malloc (size_t size)
{
  struct LocalTLS *local_tls = vdl_tls_get_local_tls ();
  struct Alloc *allocator = local_tls ? local_tls->allocator : &g_alloc;
  return alloc_malloc (allocator, size);
}

void
vdl_alloc_free (void *buffer)
{
  if (buffer)
    {
      alloc_free (buffer);
    }
}
