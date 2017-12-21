#include <dlfcn.h>
#include "test.h"
LIB(test7)

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");

  //void *g;
  //g = dlopen ("./libg.so", RTLD_LAZY);
  dlopen ("./libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");

  printf ("leave main\n");
  return 0;
}
