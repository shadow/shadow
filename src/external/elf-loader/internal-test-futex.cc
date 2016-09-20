#include "futex.h"
#include <pthread.h>
struct Futex g_futex;
unsigned int g_shared_var;

void *
futex_thread_a (void*)
{
  for (unsigned int i = 0; i < 10000; i++)
    {
      futex_lock (&g_futex);
      g_shared_var = i;
      for (unsigned int j = 0; j < 10000; j++)
	{
	  if (g_shared_var != i)
	    {
	      return (void*)-1;
	    }
	}
      futex_unlock (&g_futex);
    }
  return (void*)0;
}

void *
futex_thread_b (void*)
{
  for (unsigned int i = 10000; i > 0; i--)
    {
      futex_lock (&g_futex);
      g_shared_var = i;
      for (unsigned int j = 0; j < 10000; j++)
	{
	  if (g_shared_var != i)
	    {
	      return (void*)-1;
	    }
	}
      futex_unlock (&g_futex);
    }
  return (void*)0;
}

bool
test_futex(void)
{
  futex_construct (&g_futex);
  pthread_t tha;
  pthread_t thb;
  pthread_create (&tha, 0, &futex_thread_a, 0);
  pthread_create (&thb, 0, &futex_thread_b, 0);
  void *reta;
  void *retb;
  pthread_join (tha, &reta);
  pthread_join (thb, &retb);
  futex_destruct (&g_futex);
  return reta == 0 && retb == 0;
}
