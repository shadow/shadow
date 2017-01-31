#include "test.h"
#include <dlfcn.h>
LIB(test23);
int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  void *h = dlopen ("libm.so.6", RTLD_NOW);
  long double (*strtold)(const char *nptr, char **endptr) = NULL;
  long double db = 0.0;
  strtold = dlsym (h, "strtold");

  db = strtold ("2.444", NULL);
  printf ("%.3Lf\n", db);
  dlclose (h);
  return 0;
}
