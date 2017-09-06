#include "test.h"

LIB(h)

extern void function_g (void);

__attribute__ ((destructor)) void function_h_g (void)
{
  printf ("special destructor in libh.so\n");
  function_g ();
}
