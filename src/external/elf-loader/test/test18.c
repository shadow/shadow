#define _GNU_SOURCE 1
#include "test.h"
#include <dlfcn.h>
#include <stdio.h>
LIB(test18)

typedef int (*Fn) (int);

int main (__attribute__((unused)) int argc,
          __attribute__((unused)) char *argv[])
{
  void * h1 = dlmopen (LM_ID_NEWLM, "libp.so", RTLD_LAZY);
  void * h2 = dlmopen (LM_ID_NEWLM, "libp.so", RTLD_LAZY);
  if (h1 != h2)
    {
      printf ("loaded libp.so twice\n");
    }
  Lmid_t lmid1;
  Lmid_t lmid2;
  int status = dlinfo (h1, RTLD_DI_LMID, &lmid1);
  if (status == 0)
    {
      printf ("got lmid1\n");
    }
  status = dlinfo (h2, RTLD_DI_LMID, &lmid2);
  if (status == 0)
    {
      printf ("got lmid2\n");
    }
  if (lmid1 != lmid2)
    {
      printf ("yes, contexts are different !\n");
    }
  Fn fp1 = dlsym (h1, "libp_set_global");
  Fn fq1 = dlsym (h1, "libq_set_global");
  Fn fp2 = dlsym (h2, "libp_set_global");
  Fn fq2 = dlsym (h2, "libq_set_global");
  if (fp1(-1) == 0 &&
      fp1(2) == -1 &&
      fq1(-2) == 0 &&
      fq1(-1) == -2 &&
      fp2(-1) == 0 &&
      fp2(2) == -1 &&
      fq2(-2) == 0 &&
      fq2(-1) == -2)
    {
      printf ("both libraries have separate symbols and global variables\n");
    }

  dlclose (h2);
  dlclose (h1);

  return 0;
}
