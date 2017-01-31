#include <dlfcn.h>
#include "test.h"
LIB(test0)

int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  printf ("leave main\n");
  return 0;
}
