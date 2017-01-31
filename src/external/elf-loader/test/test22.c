#include "test.h"
#include <dlfcn.h>
LIB(test22);
int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  void *h = dlopen ("lb22.so", RTLD_LAZY);
  dlclose (h);
  return 0;
}
