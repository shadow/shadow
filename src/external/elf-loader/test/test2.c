#include <dlfcn.h>
#include "test.h"
LIB(test2)

int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  void *f = dlopen ("libf.so", RTLD_LAZY);
  printf ("dlopen libf.so completed\n");
  void *e = dlopen ("libe.so", RTLD_LAZY);
  printf ("dlopen libe.so completed\n");

  dlclose (f);
  printf ("dlclose libf.so completed\n");
  dlclose (e);
  printf ("dlclose libe.so completed\n");

  e = dlopen ("libe.so", RTLD_LAZY | RTLD_DEEPBIND);
  printf ("dlopen libe.so completed\n");
  f = dlopen ("libf.so", RTLD_LAZY | RTLD_DEEPBIND);
  printf ("dlopen libf.so completed\n");

  dlclose (e);
  printf ("dlclose libe.so completed\n");
  dlclose (f);
  printf ("dlclose libf.so completed\n");

  printf ("leave main\n");
  return 0;
}
