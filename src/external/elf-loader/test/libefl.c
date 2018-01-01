#include "test.h"

LIB(efl)

void __attribute__ ((noinline)) function_f (void)
{
  printf ("function_f in libefl.so\n");
}

void __attribute__ ((noinline)) function_l (void)
{
  printf ("function_l in libefl.so\n");
}
