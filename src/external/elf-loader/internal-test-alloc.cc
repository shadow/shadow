#include <list>
#include <string.h>
#include "alloc.h"

bool test_alloc (void)
{
  struct Alloc alloc;
  alloc_initialize (&alloc);
  int sizes[] = {0, 1, 2, 3, 4, 8, 10, 16, 19, 30, 64, 120, 240, 1020,
                 4098, 10000, 100000, 1000000};
  for (uint32_t i = 0; i < sizeof (sizes)/sizeof (int); i++)
    {
      int size = sizes[i];
      void *ptr = alloc_malloc (&alloc, size);
      memset (ptr, 0x66, size);
      alloc_free (ptr);
    }
  std::list<void *> ptrs;
  for (uint32_t i = 0; i < sizeof (sizes)/sizeof (int); i++)
    {
      int size = sizes[i];
      void *ptr = alloc_malloc (&alloc, size);
      memset (ptr, 0x66, size);
      ptrs.push_back (ptr);
    }
  for (uint32_t i = 0; i < sizeof (sizes)/sizeof (int); i++)
    {
      alloc_free (ptrs.front ());
      ptrs.pop_front ();
    }
  ptrs.clear ();

  alloc_destroy (&alloc);


  alloc_initialize (&alloc);

  void *a = alloc_malloc (&alloc, 32000);
  void *b = alloc_malloc (&alloc, 2000);
  alloc_free (a);
  alloc_free (b);

  alloc_destroy (&alloc);

  return true;
}
