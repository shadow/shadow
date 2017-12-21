#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <stdio.h>

int main (__attribute__((unused)) int argc, char *argv[])
{
  printf ("enter\n");
  void *h = dlmopen (LM_ID_NEWLM, argv[0], RTLD_LAZY | RTLD_GLOBAL);
  if (h != 0)
    {
      printf ("loaded %s second time\n", argv[0]);
    }
  dlclose (h);
  printf ("leave\n");
  return 0;
}
