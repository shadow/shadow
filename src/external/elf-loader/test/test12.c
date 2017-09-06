#include "test.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
LIB(test12)

static void *thread (__attribute__((unused)) void *ctx)
{
  void *handle = dlopen ("libl.so", RTLD_LAZY);
  if (handle == 0)
    {
      printf ("dlopen failed: %s\n", dlerror ());
    }
  if (dlerror () == 0)
    {
      printf ("error cleared\n");
    }
  return 0;
}

int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  void *handle = dlopen ("libj.so", RTLD_LAZY);
  if (handle == 0)
    {
      printf ("dlopen failed\n");
    }
  pthread_t th;
  pthread_create (&th, 0, thread, 0);

  pthread_join (th, 0);

  printf ("main error: \"%s\"\n", dlerror ());

  return 0;
}
