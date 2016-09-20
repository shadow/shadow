#include <dlfcn.h>
#include "test.h"
LIB(test7)

int main (int argc, char *argv[])
{
  printf ("enter main\n");

  void *g;
  g = dlopen ("libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");

  printf ("leave main\n");
  return 0;
}
