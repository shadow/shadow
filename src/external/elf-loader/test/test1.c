#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include "test.h"
LIB(test1)

static void * __attribute__ ((noinline)) get_pc (void)
{
  void *caller = __builtin_return_address (0);
  return caller;
}

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  void *h = dlopen ("liba.so", RTLD_LAZY);
  printf ("dlopen completed\n");

  Dl_info info;
  int status = dladdr(get_pc (), &info);
  if (status != 0)
    {
      printf ("dladdr ok file=%s, symbol=%s\n",
              info.dli_fname, info.dli_sname);
    }

  dlclose (h);
  printf ("leave main\n");
  return 0;
}
