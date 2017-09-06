#include <dlfcn.h>
#include "test.h"
LIB(test5)

int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");

  void *f = dlopen ("libf.so", RTLD_LAZY | RTLD_GLOBAL);
  printf ("dlopen libf.so completed\n");

  void *g = dlopen ("libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");
  void (*function_g_f) (void) = dlsym (g, "function_g_f");
  function_g_f ();
  dlclose (f);
  printf ("dlclose libf.so completed\n");

  dlclose (g);
  printf ("dlclose libg.so completed\n");


  f = dlopen ("libf.so", RTLD_LAZY | RTLD_GLOBAL);
  printf ("dlopen libf.so completed\n");

  g = dlopen ("libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");

  dlclose (f);
  printf ("dlclose libf.so completed\n");

  dlclose (g);
  printf ("dlclose libg.so completed\n");

  printf ("leave main\n");
  return 0;
}
