#include "test/test.h"

LIB(g)

extern void function_f (void);

void function_g_f (void)
{
  function_f ();
}
