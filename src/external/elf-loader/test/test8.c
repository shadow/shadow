#include <dlfcn.h>
#include "test.h"
LIB(test8)

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");

  void *g1;
  g1 = dlopen ("./libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");

  void *g2;
  g2 = dlopen ("./libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");

  dlclose (g1);
  printf ("dlclose libg.so completed\n");

  dlclose (g2);
  printf ("dlclose libg.so completed\n");

  printf ("leave main\n");
  return 0;
}
