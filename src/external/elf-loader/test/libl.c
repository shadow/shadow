#include "test.h"

LIB(l)

extern void call_function_f (void);
void call_function_l_f (void)
{
  printf ("calling call_function_f from libl.so\n");
  call_function_f ();
}
