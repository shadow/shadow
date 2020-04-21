#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include "test/test.h"
LIB(test10)

static int *(*g_get_i) (void) = 0;
static sem_t g_wait_a;
static sem_t g_wait_b;

static void *thread (__attribute__((unused)) void*ctx)
{
  int *i = g_get_i ();
  printf ("th i=%d\n", *i);
  *i = 2;
  printf ("th i=%d\n", *i);
  sem_post (&g_wait_a);
  sem_wait (&g_wait_b);
  i = g_get_i ();
  printf ("th i=%d\n", *i);
  *i = 2;
  printf ("th i=%d\n", *i);
  return 0;
}

static void
test_one (void)
{
  void *handle = dlopen ("./libi.so", RTLD_LAZY);

  g_get_i = (int *(*) (void)) dlsym (handle, "get_i");
  int *i = g_get_i ();
  printf ("main i=%d\n", *i);

  sem_init (&g_wait_a, 0, 0);
  sem_init (&g_wait_b, 0, 0);
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_t th;
  pthread_create (&th, &attr, thread, 0);

  sem_wait (&g_wait_a);
  dlclose (handle);
  handle = dlopen ("./libi.so", RTLD_LAZY);

  g_get_i = (int *(*) (void)) dlsym (handle, "get_i");
  i = g_get_i ();
  printf ("main i=%d\n", *i);
  sem_post (&g_wait_b);

  pthread_join (th, 0);
  printf ("main i=%d\n", *i);

  dlclose (handle);
}

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");

  test_one ();
  test_one ();

  printf ("leave main\n");
  return 0;
}
