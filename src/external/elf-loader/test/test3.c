#include <dlfcn.h>
#include "test.h"
LIB(test3)


int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  void *f = dlopen ("libf.so", RTLD_LAZY);
  printf ("dlopen libf.so completed\n");
  void (*function_f) (void) = dlsym (f, "function_f");
  function_f ();
  dlclose (f);
  printf ("dlclose libf.so completed\n");
  printf ("leave main\n");
  return 0;
}
