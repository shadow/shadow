#include "test/test.h"

LIB(t)

static int
function_t2_internal (int *i)
{
  *i = -1;
  return 1;
}

void *function_t2_ifunc (void) __asm__ ("function_t2");

void *
function_t2_ifunc (void)
{
  // to be extra sure GCC won't optimize this out
  volatile int t = 1;
  if(t)
    return &function_t2_internal;
  return NULL;
}
asm (".type function_t2, %gnu_indirect_function");
