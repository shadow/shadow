#include "test.h"
#include <dlfcn.h>
LIB(test22);
int main (int argc, char *argv[])
{
  void *h = dlopen ("lb22.so", RTLD_LAZY);
  dlclose (h);
  return 0;
}
