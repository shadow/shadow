#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include "test.h"
LIB(test25)

__thread int g_a = 0;

static void *thread (void*ctx)
{
  void *h;
  printf("a=%d\n", g_a);
  h = dlopen("libr.so", RTLD_LAZY);
  dlclose(h);
  g_a = 10;
  return 0;
}

int main (__attribute__((unused) int argc,
          __attribute__((unused)char *argv[])
{
  int i;
  pthread_t th[100];
  printf ("enter main\n");

  for (i = 0; i <sizeof(th)/sizeof(pthread_t); i++)
    {
      pthread_attr_t attr;
      pthread_attr_init (&attr);
      g_a = 2;
      pthread_create (&th[i], &attr, thread, 0);

      printf ("main a=%d\n", g_a);
    }

  for (i = 0; i <sizeof(th)/sizeof(pthread_t); i++)
    {
      void *retval;
      pthread_join(th[i], &retval);
    }


  printf ("leave main\n");
  return 0;
}
