// tests support for GNU ifunc extension
#include <dlfcn.h>
#include "test.h"
LIB(test27)

int main (int argc, char *argv[])
{
  int i = 0;
  int j = 0;
  printf ("enter main\n");
  void *s = dlopen ("libs.so", RTLD_LAZY);
  printf ("dlopen libs.so completed\n");

  // test the relocated symbol path (i.e. do_process_reloc)
  int (*function_s2t) (int *) = dlsym (s, "function_s2t");
  printf ("found function_s2t\n");
  j = function_s2t (&i);
  printf ("function_s2t sets i to %d, returns %d\n", i, j);

  // test the normal path (i.e. vdl_sym_with_flags)
  int (*function_s2) (int *) = dlsym (s, "function_s2");
  printf ("found function_s2\n");
  j = function_s2 (&i);
  printf ("function_s2 sets i to %d, returns %d\n", i, j);

  // there is one more potential source of ifuncs (in vdl_lookup_local),
  // but it's currently only accessible internally

  dlclose (s);
  printf ("dlclose libs.so completed\n");
  printf ("leave main\n");
}
