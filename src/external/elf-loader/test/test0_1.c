#include <dlfcn.h>
#include "test.h"
LIB(test01)

int main (int argc, char *argv[])
{
  printf ("enter main\n");
  printf ("leave main\n");
  return 0;
}
