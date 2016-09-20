#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include "test.h"
LIB(test25)

__thread int g_a = 0;

static void *thread (void*ctx)
{
  printf("a=%d\n", g_a);
  g_a = 10;
  return 0;
}

int main (int argc, char *argv[])
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
    }

  for (i = 0; i <sizeof(th)/sizeof(pthread_t); i++)
    {
      void *retval;
      pthread_join(th[i], &retval);
    }


  printf ("leave main\n");
  return 0;
}
