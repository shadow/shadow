#include <dlfcn.h>
#include <stdio.h>
#include "test.h"

LIB(test29)

// this test tests large dlmopen counts
#define DLMOPEN_COUNT 1000

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  void *handles[DLMOPEN_COUNT];
  dlerror();
  for (int i = 0; i < DLMOPEN_COUNT; i++)
    {
      handles[i] = dlmopen(LM_ID_NEWLM, "./libr.so", RTLD_LAZY);
      if (!handles[i])
        {
          printf ("failed to open handle %d: %s\n", i, dlerror());
          return 1;
        }
      Lmid_t lmid;
      if(dlinfo (handles[i], RTLD_DI_LMID, &lmid))
        {
          printf ("dlinfo failed on iteration %d: %s\n", i, dlerror());
          return 1;
        }
      // make sure we actually use some TLS in every namespace,
      // even though there are no other threads
      int (*fn1)(void) = dlsym (handles[i], "get_b");
      if (!fn1)
        {
          printf ("failed to find %d get_b(): %s\n", i, dlerror());
          return 1;
        }
      int b = fn1();
      void (*fn2)(int) = dlsym (handles[i], "set_b");
      if (!fn1)
        {
          printf ("failed to find %d set_b(): %s\n", i, dlerror());
          return 1;
        }
      fn2 (b+1);
    }
  for (int i = 0; i < DLMOPEN_COUNT; i++)
    {
      if(dlclose (handles[i]))
        {
          printf ("failed to close %d: %s\n", i, dlerror());
          return 1;
        }
    }
  printf ("leave main\n");
  return 0;
}
