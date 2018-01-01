#include "vdl-mem.h"
#include <stdint.h>

void *
memcpy (void *d, const void *s, size_t len)
{
  int tmp = len;
  char *dst = d;
  const char *src = s;
  while (tmp > 0)
    {
      *dst = *src;
      dst++;
      src++;
      tmp--;
    }
  return d;
}

void
vdl_memcpy (void *d, const void *s, size_t len)
{
  memcpy (d, s, len);
}

void
vdl_memmove (void *dst, const void *src, size_t len)
{
  uint8_t *ss = (uint8_t *) src;
  uint8_t *se = ss + len;
  uint8_t *ds = (uint8_t *) dst;
  uint8_t *de = ds + len;
  if (ss > de || se <= ds)
    {
      // no overlap
      vdl_memcpy (dst, src, len);
    }
  else if (de > se)
    {
      // overlap
      unsigned long size = (unsigned long) de - (unsigned long) se;
      vdl_memcpy (de - size, se - size, size);
      // recursively finish the copy
      vdl_memmove (dst, src, len - size);
    }
  else if (ds < ss)
    {
      // overlap
      unsigned long size = (unsigned long) ss - (unsigned long) ds;
      vdl_memcpy (ds, ss, size);
      // recursively finish the copy
      vdl_memmove (ds + size, ss + size, len - size);
    }
  else
    {
      int *p = 0;
      *p = 0;
    }
}

void *
memset (void *d, int c, size_t n)
{
  char *dst = d;
  size_t i;
  for (i = 0; i < n; i++)
    {
      dst[i] = c;
    }
  return d;
}

void
vdl_memset (void *d, int c, size_t n)
{
  memset(d, c, n);
}

int
vdl_memcmp (void *a, void *b, size_t n)
{
  uint8_t *s1 = a;
  uint8_t *s2 = b;
  size_t i;
  for (i = 0; i < n; i++)
    {
      if (*s1 < *s2)
        {
          return -1;
        }
      else if (*s1 > *s2)
        {
          return +1;
        }
    }
  return 0;
}
