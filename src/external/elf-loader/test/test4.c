#include <dlfcn.h>
#include "test.h"
LIB(test4)

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  void *f = dlopen ("libf.so", RTLD_LAZY);
  printf ("dlopen libf.so completed\n");
  void (*function_f_e) (void) = dlsym (f, "function_f_e");
  void *e = dlopen ("libe.so", RTLD_LAZY | RTLD_GLOBAL);
  printf ("dlopen libe.so completed\n");
  function_f_e ();
  dlclose (e);
  printf ("dlclose libe.so completed\n");
  dlclose (f);
  printf ("dlclose libf.so completed\n");

  f = dlopen ("libf.so", RTLD_LAZY);
  printf ("dlopen libf.so completed\n");
  function_f_e = dlsym (f, "function_f_e");
  e = dlopen ("libe.so", RTLD_LAZY);
  printf ("dlopen libe.so completed\n");
  function_f_e ();
  dlclose (e);
  printf ("dlclose libe.so completed\n");
  dlclose (f);
  printf ("dlclose libf.so completed\n");

  printf ("leave main\n");
  return 0;
}
