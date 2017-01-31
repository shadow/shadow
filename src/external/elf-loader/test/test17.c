#include <string.h>
#include <stdlib.h>

int foo (const char *str)
{
  char *copy = strdup(str);
  int len = strlen(copy);
  free (copy);
  return len >=2;
}
int main (__attribute__((unused)) int argc,
	  __attribute__((unused)) char *argv[])
{
  foo("targeted");
  return 0;
}
