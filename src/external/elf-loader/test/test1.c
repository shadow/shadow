#define _GNU_SOURCE
#include <dlfcn.h>
#include "test.h"
LIB(test1)

static void * get_pc (void)
{
  void *caller = __builtin_return_address (0);
  return caller;
}
int main (int argc, char *argv[])
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
