#include <string.h>
#include <stdlib.h>

int foo (const char *str)
{
  char *copy = strdup(str);
  int len = strlen(copy);
  free (copy);
  return len >=2;
}
int main (int argc, char *argv[])
{
  foo("targeted");
  return 0;
}
