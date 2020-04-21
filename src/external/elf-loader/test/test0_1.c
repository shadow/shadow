#include <dlfcn.h>
#include "test/test.h"
LIB(test01)

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  printf ("leave main\n");
  return 0;
}
