#include <dlfcn.h>

extern void function_e (void);

void call_function_l (void)
{}

int main (int argc, char *argv[])
{
  void *h1 = dlopen ("libf.so", RTLD_GLOBAL | RTLD_LAZY);
  void *h2 = dlopen ("libf.so", RTLD_LAZY);
  dlclose (h1);
  function_e ();
  dlclose (h2);

  return 0;
}
