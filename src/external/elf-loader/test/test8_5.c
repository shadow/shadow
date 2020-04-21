#include "test/test.h"
#include <pthread.h>
#include <errno.h>
#include <string.h>
LIB(test8_5)


static void *thread (__attribute__((unused)) void *ctx)
{
  return 0;
}

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  pthread_t th;
  int status = pthread_create (&th, 0, &thread, 0);
  if (status != 0)
    {
      return 4;
    }
  void *retval;
  status = pthread_join (th, &retval);
  if (status != 0 || retval != 0)
    {
      printf ("errno=%d/\"%s\"\n", status, strerror (status));
      return 6;
    }
  return 0;
}
