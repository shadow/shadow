#include "test.h"

LIB(s)

extern int function_t2 (int *i);

int
function_s2t (int *i)
{
  return function_t2(i);
}

static int
function_s2_internal (int *i)
{
  *i = 1;
  return -1;
}

void *function_s2_ifunc (void) __asm__ ("function_s2");

void *
function_s2_ifunc (void)
{
  // to be extra sure GCC won't optimize this out
  volatile int t = 1;
  if(t)
    return &function_s2_internal;
  return NULL;
}
asm (".type function_s2, %gnu_indirect_function");
