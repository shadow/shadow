#include "test/test.h"

LIB(f)

extern void function_e (void);

void __attribute__ ((noinline)) function_e (void)
{
  printf ("called function_e in libf.so\n");
}

void __attribute__ ((noinline)) function_f_e (void)
{
  function_e ();
}
extern void call_function_l (void);
void call_function_f_l (void)
{
  printf ("calling call_function_l from libf.so\n");
  call_function_l ();
}
