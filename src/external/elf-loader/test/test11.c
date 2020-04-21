#include "test/test.h"
#include <stdlib.h>
LIB(test11)

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  void *p = malloc (10);
  free (p);
  return 0;
}
