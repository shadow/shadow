#include "test/test.h"
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>
LIB(test9)

static __thread int g_count = 0;
static sem_t g_sem_a;
static sem_t g_sem_b;

static void *thread_a (__attribute__((unused)) void *ctx)
{
  while (g_count < 10)
    {
      int status = sem_wait (&g_sem_a);
      if (status == -1)
        {
          return (void*)-1;
        }
      printf ("a=%d\n", g_count);
      status = sem_post (&g_sem_b);
      if (status == -1)
        {
          return (void*)-1;
        }
      g_count++;
    }
  return 0;
}

static void *thread_b (__attribute__((unused)) void *ctx)
{
  while (g_count < 10)
    {
      int status = sem_wait (&g_sem_b);
      if (status == -1)
        {
          return (void*)-1;
        }
      printf ("b=%d\n", g_count);
      status = sem_post (&g_sem_a);
      if (status == -1)
        {
          return (void*)-1;
        }
      g_count++;
    }
  return 0;
}


int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  int status = sem_init (&g_sem_a, 0, 1);
  if (status == -1)
    {
      return 1;
    }
  status = sem_init (&g_sem_b, 0, 0);
  if (status == -1)
    {
      return 2;
    }
  pthread_attr_t attr;
  status = pthread_attr_init(&attr);
  if (status != 0)
    {
      return 3;
    }
  pthread_t tha;
  status = pthread_create (&tha, &attr, &thread_a, 0);
  if (status != 0)
    {
      return 4;
    }
  pthread_t thb;
  status = pthread_create (&thb, &attr, &thread_b, 0);
  if (status != 0)
    {
      return 5;
    }
  void *retval;
  status = pthread_join (tha, &retval);
  if (status != 0 || retval != 0)
    {
      printf ("errno=%d/\"%s\"\n", status, strerror (status));
      return 6;
    }
  status = pthread_join (thb, &retval);
  if (status != 0 || retval != 0)
    {
      return 7;
    }
  return 0;
}
