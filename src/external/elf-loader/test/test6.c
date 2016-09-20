#include <dlfcn.h>
#include "test.h"
LIB(test6)

int main (int argc, char *argv[])
{
  printf ("enter main\n");

  void *f = dlopen ("libf.so", RTLD_LAZY | RTLD_GLOBAL);
  printf ("dlopen libf.so completed\n");

  void *g = dlopen ("libg.so", RTLD_LAZY);
  printf ("dlopen libg.so completed\n");
  void (*function_g_f) (void) = dlsym (g, "function_g_f");
  function_g_f ();

  // libf.so will not be unloaded until we dlclose libg.so
  // because the call to function_g_f above creates
  // a dependency from g to f.
  dlclose (f);
  printf ("dlclose libf.so completed\n");

  void *h = dlopen ("libh.so", RTLD_LAZY);
  printf ("dlopen libh.so completed\n");
  void (*function_h_g) (void) = dlsym (h, "function_h_g");
  function_h_g ();

  dlclose (g);
  printf ("dlclose libg.so completed\n");

  dlclose (h);
  printf ("dlclose libh.so completed\n");


  printf ("leave main\n");
  return 0;
}
