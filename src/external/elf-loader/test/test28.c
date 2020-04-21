#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <semaphore.h>
#include <unistd.h>
#include "test/test.h"
#include "../vdl-dl-public.h"
LIB(test28)

struct Args
{
  void *handle;
  int thread_number;
  pthread_barrier_t *internal_barrier;
  pthread_barrier_t *external_barrier;
};

static void *thread (void *ctx)
{
  struct Args *args = ctx;
  int (*fn1)(void) = dlsym (args->handle, "get_b");
  // make an effort at printing in thread order
  usleep (100000 * args->thread_number);
  printf ("b=%d on %d\n", fn1(), args->thread_number);
  // make sure all other threads have printed initial b value
  pthread_barrier_wait (args->internal_barrier);
  void (*fn2)(int) = dlsym (args->handle, "set_b");
  fn2 (args->thread_number);
  usleep (100000 * args->thread_number);
  printf ("set b=%d on %d\n", fn1(), args->thread_number);
  // let the main thread know we're ready
  pthread_barrier_wait (args->external_barrier);
  // wait for the main thread to (maybe) swap our TLS
  pthread_barrier_wait (args->external_barrier);
  usleep (100000 * args->thread_number);
  printf ("now b=%d on %d\n", fn1(), args->thread_number);
  // let the main thread know we're done
  pthread_barrier_wait (args->external_barrier);
  return 0;
}

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");
  void *h = dlmopen(LM_ID_NEWLM, "./libr.so", RTLD_LAZY);
  Lmid_t lmid;
  dlinfo (h, RTLD_DI_LMID, &lmid);
  pthread_t th[3];
  struct Args args[3];
  pthread_barrier_t internal_barrier, external_barrier;
  pthread_barrier_init (&internal_barrier, NULL, 3);
  pthread_barrier_init (&external_barrier, NULL, 4);
  unsigned int i;
  for (i = 0; i <sizeof(th)/sizeof(pthread_t); i++)
    {
      args[i].handle = h;
      args[i].thread_number = i;
      args[i].internal_barrier = &internal_barrier;
      args[i].external_barrier = &external_barrier;
      pthread_attr_t attr;
      pthread_attr_init (&attr);
      pthread_create (&th[i], &attr, thread, &args[i]);
    }
  pthread_barrier_wait (&external_barrier);
  vdl_dl_lmid_swap_tls_public (lmid, &th[0], &th[1]);
  pthread_barrier_wait (&external_barrier);
  pthread_barrier_wait (&external_barrier);
  pthread_barrier_destroy(&internal_barrier);
  pthread_barrier_destroy(&external_barrier);
  printf ("leave main\n");
  return 0;
}
