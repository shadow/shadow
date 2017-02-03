#include "vdl-utils.h"
#include <stdarg.h>
#include <sys/stat.h>
#include "system.h"
#include "vdl.h"
#include "vdl-log.h"
#include "vdl-mem.h"
#include "vdl-alloc.h"
#include "avprintf-cb.h"


int
vdl_utils_strisequal (const char *a, const char *b)
{
  //VDL_LOG_FUNCTION ("a=%s, b=%s", a, b);
  while (*a != 0 && *b != 0)
    {
      if (*a != *b)
        {
          return 0;
        }
      a++;
      b++;
    }
  return *a == *b;
}

int
vdl_utils_strlen (const char *str)
{
  //VDL_LOG_FUNCTION ("str=%s", str);
  int len = 0;
  while (str[len] != 0)
    {
      len++;
    }
  return len;
}

char *
vdl_utils_strdup (const char *str)
{
  if (str == 0)
    {
      return 0;
    }
  //VDL_LOG_FUNCTION ("str=%s", str);
  int len = vdl_utils_strlen (str);
  char *retval = vdl_alloc_malloc (len + 1);
  vdl_memcpy (retval, str, len + 1);
  return retval;
}

char *
vdl_utils_strfind (char *str, const char *substr)
{
  char *cur = str;
  while (*cur != 0)
    {
      char *a = cur;
      const char *b = substr;
      while (*a != 0 && *b != 0 && *a == *b)
        {
          a++;
          b++;
        }
      if (*b == 0)
        {
          return cur;
        }
      cur++;
    }
  return 0;
}

char *
vdl_utils_strconcat (const char *str, ...)
{
  VDL_LOG_FUNCTION ("str=%s", str);
  va_list l1, l2;
  uint32_t size;
  char *cur, *retval, *tmp;
  size = vdl_utils_strlen (str);
  va_start (l1, str);
  va_copy (l2, l1);
  // calculate size of final string
  cur = va_arg (l1, char *);
  while (cur != 0)
    {
      size += vdl_utils_strlen (cur);
      cur = va_arg (l1, char *);
    }
  va_end (l1);
  retval = vdl_alloc_malloc (size + 1);
  // copy first string
  tmp = retval;
  vdl_memcpy (tmp, str, vdl_utils_strlen (str));
  tmp += vdl_utils_strlen (str);
  // concatenate the other strings.
  cur = va_arg (l2, char *);
  while (cur != 0)
    {
      vdl_memcpy (tmp, cur, vdl_utils_strlen (cur));
      tmp += vdl_utils_strlen (cur);
      cur = va_arg (l2, char *);
    }
  // append final 0
  *tmp = 0;
  va_end (l2);
  return retval;
}

unsigned long
vdl_utils_strtoul (const char *integer)
{
  unsigned long ret;
  int i, d;
  for (i = 0, ret = 0; integer[i] != '\0'; i++)
    {
      d = integer[i] - '0';
      if (d < 0 || d > 9)
        {
          continue;
        }
      ret = ret * 10 + d;
    }
  return ret;
}

// puts a 10 digit decimal representation of value in the str buffer
// does *not* null terminate
void
vdl_utils_itoa (unsigned long value, char *str)
{
  // largest 32 bit int is 10 decimal digits long,
  // into a 0-indexed char array
  int i = 10 - 1;
  for (; i >= 0; i--)
    {
      str[i] = '0' + value % 10;
      value /= 10;
    }
}

uint32_t
vdl_gnu_hash (const char *s)
{
  // Copy/paste from the glibc source code.
  // This function is coming from comp.lang.c and was originally
  // posted by Daniel J Bernstein
  uint32_t h = 5381;
  unsigned char c;
  for (c = *s; c != '\0'; c = *++s)
    {
      h = h * 33 + c;
    }
  return h;
}

int
vdl_utils_exists (const char *filename)
{
  VDL_LOG_FUNCTION ("filename=%s", filename);
  struct stat buf;
  int status = system_fstat (filename, &buf);
  return status == 0;
}

const char *
vdl_utils_getenv (const char **envp, const char *value)
{
  VDL_LOG_FUNCTION ("envp=%p, value=%s", envp, value);
  while (*envp != 0)
    {
      const char *env = *envp;
      const char *tmp = value;
      while (*tmp != 0 && *env != 0)
        {
          if (*tmp != *env)
            {
              goto next;
            }
          env++;
          tmp++;
        }
      if (*env != '=')
        {
          goto next;
        }
      env++;
      return env;
    next:
      envp++;
    }
  return 0;
}

void
vdl_utils_str_list_delete (struct VdlList *list)
{
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list); i = vdl_list_next (i))
    {
      vdl_alloc_free (*i);
    }
  vdl_list_delete (list);
}

struct VdlList *
vdl_utils_strsplit (const char *value, char separator)
{
  VDL_LOG_FUNCTION ("value=%s, separator=%d", (value == 0) ? "" : value,
                    separator);
  struct VdlList *list = vdl_list_new ();
  const char *prev = value;
  const char *cur = value;

  if (value == 0)
    {
      return list;
    }
  while (1)
    {
      size_t prev_len;
      char *str;
      while (*cur != separator && *cur != 0)
        {
          cur++;
        }
      prev_len = cur - prev;
      str = vdl_alloc_malloc (prev_len + 1);
      vdl_memcpy (str, prev, prev_len);
      str[prev_len] = 0;
      vdl_list_push_back (list, str);
      if (*cur == 0)
        {
          break;
        }
      cur++;
      prev = cur;
    }
  return list;
}

struct VdlList *
vdl_utils_splitpath (const char *value)
{
  struct VdlList *list = vdl_utils_strsplit (value, ':');
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list); i = vdl_list_next (i))
    {
      if (vdl_utils_strisequal (*i, ""))
        {
          // the empty string is interpreted as '.'
          vdl_alloc_free (*i);
          i = vdl_list_erase (list, i);
          i = vdl_list_insert (list, i, vdl_utils_strdup ("."));
        }
    }
  return list;
}


unsigned long
vdl_utils_align_down (unsigned long v, unsigned long align)
{
  if ((v % align) == 0)
    {
      return v;
    }
  unsigned long aligned = v - (v % align);
  return aligned;
}

unsigned long
vdl_utils_align_up (unsigned long v, unsigned long align)
{
  if ((v % align) == 0)
    {
      return v;
    }
  unsigned long aligned = v + align - (v % align);
  return aligned;
}

ElfW (Phdr) * vdl_utils_search_phdr (ElfW (Phdr) * phdr, int phnum, ElfW (Word) type)
{
  VDL_LOG_FUNCTION ("phdr=%p, phnum=%d, type=%d", phdr, phnum, type);
  ElfW (Phdr) * cur;
  int i;
  for (cur = phdr, i = 0; i < phnum; cur++, i++)
    {
      if (cur->p_type == type)
        {
          return cur;
        }
    }
  return 0;
}

// Note that this implementation is horribly inneficient but it's
// also incredibly simple. A more efficient implementation would
// pre-allocate a large string buffer and would create a larger
// buffer only when needed to avoid the very very many memory
// allocations and frees done for each caracter.
static void
avprintf_callback (char c, void *context)
{
  if (c != 0)
    {
      char **pstr = (char **) context;
      char new_char[] = { c, 0 };
      char *new_str = vdl_utils_strconcat (*pstr, new_char, 0);
      vdl_alloc_free (*pstr);
      *pstr = new_str;
    }
}


char *
vdl_utils_vprintf (const char *str, va_list args)
{
  char *retval = vdl_utils_strdup ("");
  int status = avprintf_cb (avprintf_callback, &retval, str, args);
  if (status < 0)
    {
      return 0;
    }
  return retval;
}


char *
vdl_utils_sprintf (const char *str, ...)
{
  va_list list;
  va_start (list, str);
  char *string = vdl_utils_vprintf (str, list);
  va_end (list);
  return string;
}
