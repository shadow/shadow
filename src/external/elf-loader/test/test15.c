#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include "test.h"
LIB(test15)

typedef void (*Function)(void);

void
function_f (void)
{
  printf ("function_f in main\n");
}

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  printf ("enter main\n");

  const char *error;
  Function fn;
  void *handle;

  // clear error state
  dlerror ();

  // first, attempt RTLD_DEFAULT
  fn = dlsym (RTLD_DEFAULT, "function_f");
  error = dlerror ();
  if (error != 0)
    {
      printf ("error looking up function_f in RTLD_DEFAULT\n");
    }

  // ok, open main file and check in it
  handle = dlopen ((void*)0, RTLD_LAZY | RTLD_GLOBAL);
  error = dlerror ();
  if (error == 0)
    {
      printf ("dlopen main ok\n");
    }
  fn = dlsym (handle, "function_f");
  error = dlerror ();
  if (error != 0)
    {
      printf ("error looking up function_f in main\n");
    }
  dlclose (handle);
  error = dlerror (); // clear error

  // ok, now, let's try to see in libf.so
  handle = dlopen ("./libf.so", RTLD_LAZY);
  error = dlerror ();
  if (error == 0)
    {
      printf ("dlopen libf.so ok\n");
    }
  fn = dlsym (handle, "function_f");
  error = dlerror ();
  if (error == 0)
    {
      fn ();
    }
  // it's still not there in the global scope
  fn = dlsym (RTLD_DEFAULT, "function_f");
  error = dlerror ();
  if (error != 0)
    {
      printf ("error looking up function_f in RTLD_DEFAULT\n");
    }
  // Now, try to see if function_f in main is interposed
  // between call_function_f in libf.so and function_f in libf.so
  fn = dlsym (handle, "call_function_f");
  error = dlerror ();
  if (error == 0)
    {
      // it's not interposed
      fn ();
    }
  dlclose (handle);
  error = dlerror (); // clear error

  // ok, let's see what we can find in libef.so
  handle = dlopen ("./libefl.so", RTLD_LAZY);
  error = dlerror ();
  if (error == 0)
    {
      printf ("dlopen libefl.so ok\n");
    }
  // function_f in libef.so is interposed
  // before function_f in libf.so
  fn = dlsym (handle, "function_f");
  error = dlerror ();
  if (error == 0)
    {
      fn ();
    }
  // yes, it's really interposed, even from libf.so
  fn = dlsym (handle, "call_function_f");
  error = dlerror ();
  if (error == 0)
    {
      fn ();
    }
  // try to see if libf.so can call into libl.so
  fn = dlsym (handle, "call_function_f_l");
  error = dlerror ();
  if (error == 0)
    {
      // yes, we can !
      fn ();
    }
  // try to see if libl.so can call into libf.so
  fn = dlsym (handle, "call_function_l_f");
  error = dlerror ();
  if (error == 0)
    {
      // yes, we can !
      fn ();
    }

  {
    void *other_handle = dlopen ("./libf.so", RTLD_LAZY);
    error = dlerror ();
    if (error == 0)
      {
        printf ("reopen libf.so\n");
      }
    // it's not interposed here !
    fn = dlsym (other_handle, "function_f");
    error = dlerror ();
    if (error == 0)
      {
        fn ();
      }
    // and we can't look this up from here.
    fn = dlsym (other_handle, "function_l");
    error = dlerror ();
    if (error != 0)
      {
        printf ("dlsym is not performing lookups according to local scope\n");
      }
    dlclose (other_handle);
    error = dlerror ();
  }
  dlclose (handle);
  error = dlerror (); // clear error

  // now, let's see what RTLD_GLOBAL does
  handle = dlopen ("./libf.so", RTLD_LAZY | RTLD_GLOBAL);
  error = dlerror ();
  if (error == 0)
    {
      printf ("dlopen libf.so ok\n");
    }
  fn = dlsym (handle, "function_f");
  error = dlerror ();
  if (error == 0)
    {
      fn ();
    }
  // ooh, now, it's there in the global scope
  fn = dlsym (RTLD_DEFAULT, "function_f");
  error = dlerror ();
  if (error == 0)
    {
      fn ();
    }
  dlclose (handle);
  error = dlerror (); // clear error

  fn = dlvsym (RTLD_DEFAULT, "vdl_dl_iterate_phdr_public", "VDL_DL");
  if (fn != 0)
    {
      printf ("dlvsym works\n");
    }

  printf ("leave main\n");
  return 0;
}
