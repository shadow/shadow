/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

/*
 * This code depends on the va_arg functionality as provided by your compiler
 * and defined by the C standard.
 * It also depends on the headers mcdecl.h and mtests.h both which
 * are trivial to re-implement for any target.
 */

#include <stdarg.h>

#include "avprintf-cb.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef HAVE_STDINT_H
/* for uintmax_t */
#include <stdint.h>
#endif /* HAVE_STDINT_H */
#ifdef HAVE_STDDEF_H
/* for size_t and ptrdiff_t */
#include <stddef.h>
#endif /* HAVE_STDDEF_H */

#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif /* max */

#define avprintf_output(c) \
(*cb) (c, context);

enum parsing_state_t
{
  CONVERSION_SEEKING = 1 << 0,
  CONVERSION_NEXT = 1 << 1,
  CONVERSION_PERCENT = 1 << 2,
  CONVERSION_F = 1 << 3,
  CONVERSION_FW = 1 << 4,
  CONVERSION_FWP = 1 << 5,
  CONVERSION_FWP_h = 1 << 6,
  CONVERSION_FWP_l = 1 << 7,
  CONVERSION_FWPL = 1 << 8,
};

enum flags_t
{
  FLAG_ALTERNATE = 1 << 0,
  FLAG_ZERO = 1 << 1,
  FLAG_ADJUSTED = 1 << 2,
  FLAG_SPACE = 1 << 3,
  FLAG_PLUS = 1 << 4,

  FLAG_hh_LEN = 1 << 5,
  FLAG_h_LEN = 1 << 6,
  FLAG_l_LEN = 1 << 7,
  FLAG_ll_LEN = 1 << 8,
  FLAG_L_LEN = 1 << 9,
  FLAG_j_LEN = 1 << 10,
  FLAG_z_LEN = 1 << 11,
  FLAG_t_LEN = 1 << 12,

  FLAG_x_CNV = 1 << 17,
  FLAG_X_CNV = 1 << 18,
  FLAG_o_CNV = 1 << 15,

  /* None of the following are really useful. */
  FLAG_i_CNV = 1 << 14,
  FLAG_u_CNV = 1 << 16,
  FLAG_e_CNV = 1 << 19,
  FLAG_E_CNV = 1 << 20,
  FLAG_f_CNV = 1 << 21,
  FLAG_F_CNV = 1 << 22,
  FLAG_g_CNV = 1 << 23,
  FLAG_G_CNV = 1 << 24,
  FLAG_a_CNV = 1 << 25,
  FLAG_A_CNV = 1 << 26,
  FLAG_c_CNV = 1 << 27,
  FLAG_s_CNV = 1 << 28,
  FLAG_p_CNV = 1 << 29,

  FLAG_PRECISION = 1 << 30,
  /* 31, 13 */
};


enum
{
  CHAR_NUL = 0,
  CHAR_SPACE = 0x20,
  CHAR_POUND = 0x23,
  CHAR_DOLLAR = 0x24,
  CHAR_PERCENT = 0x25,
  CHAR_LPAREN = 0x28,
  CHAR_RPAREN = 0x29,
  CHAR_STAR = 0x2a,
  CHAR_PLUS = 0x2b,
  CHAR_MINUS = 0x2d,
  CHAR_DOT = 0x2e,
  CHAR_0 = 0x30,
  CHAR_1 = 0x31,
  CHAR_2 = 0x32,
  CHAR_3 = 0x33,
  CHAR_4 = 0x34,
  CHAR_5 = 0x35,
  CHAR_6 = 0x36,
  CHAR_7 = 0x37,
  CHAR_8 = 0x38,
  CHAR_9 = 0x39,
  CHAR_A = 0x41,
  CHAR_C = 0x43,
  CHAR_E = 0x45,
  CHAR_F = 0x46,
  CHAR_G = 0x47,
  CHAR_L = 0x4c,
  CHAR_S = 0x53,
  CHAR_X = 0x58,
  CHAR_Z = 0x5a,
  CHAR_a = 0x61,
  CHAR_c = 0x63,
  CHAR_d = 0x64,
  CHAR_e = 0x65,
  CHAR_f = 0x66,
  CHAR_g = 0x67,
  CHAR_h = 0x68,
  CHAR_i = 0x69,
  CHAR_j = 0x6a,
  CHAR_l = 0x6c,
  CHAR_n = 0x6e,
  CHAR_o = 0x6f,
  CHAR_p = 0x70,
  CHAR_q = 0x71,
  CHAR_s = 0x73,
  CHAR_t = 0x74,
  CHAR_u = 0x75,
  CHAR_x = 0x78,
  CHAR_z = 0x7a,
};

enum validation_bug_t
{
  VALID = 0,
  INVALID_CONTROL,
  INVALID_NONASCII,
};

struct conversion_t
{
  enum flags_t flags;
  int width;
  int precision;
  char base;
};

static void
maprintf_cb (avprintf_callback_t cb, void *context, char const *str, ...)
{
  va_list list;

  va_start (list, str);
  avprintf_cb (cb, context, str, list);
  va_end (list);
}

static enum validation_bug_t
validate_ascii_7_bit (unsigned char c)
{
  if ((c >= 0x01 && c <= 0x06) || (c >= 0x0e && c <= 0x1f) || c == 0x7f)
    {
      return INVALID_CONTROL;
    }
  else if (c >= 0x80)
    {
      return INVALID_NONASCII;
    }
  else
    {
      return VALID;
    }
}

static unsigned long long
local_power (unsigned char value, unsigned char power)
{
  unsigned long long llu = 1;

  /* Yes, I know, we can optimize this. doh me! */
  for (llu = 1; power > 0; power--)
    {
      llu *= value;
    }

  return llu;
}

static void
output_unsigned_int (avprintf_callback_t cb, void *context,
                     struct conversion_t const *params, unsigned long long v)
{
  char char_nb;
  signed char left_zeroes, left_spaces, right_spaces, spaces;
  unsigned long long v_copy = v;
  signed int precision = params->precision;

  char_nb = 0;
  while (v_copy != 0)
    {
      v_copy /= params->base;
      char_nb++;
    }

  left_zeroes = precision - char_nb;
  spaces = params->width - max (precision, char_nb);

  if (v && (params->flags & FLAG_ALTERNATE) &&
      (params->flags & (FLAG_x_CNV | FLAG_X_CNV)))
    {
      spaces -= 2;
    }

  if (params->flags & FLAG_ADJUSTED)
    {
      right_spaces = spaces;
      left_spaces = 0;
    }
  else
    {
      right_spaces = 0;
      left_spaces = spaces;
    }

  if (params->flags & FLAG_ZERO && !(params->flags & FLAG_PRECISION))
    {
      left_zeroes = left_spaces;
      left_spaces = 0;
    }

  if ((params->flags & FLAG_ALTERNATE) && (params->flags & FLAG_o_CNV)
      && left_zeroes <= 0)
    {
      left_zeroes = 1;
      left_spaces -= 1;
      right_spaces -= 1;
    }

#if 0
  MTEST_BOOTSTRAP_APRINTF
    ("width: %d, precision: %d, base: %d, left zeroes: %d, "
     "right spaces: %d, left spaces: %d, char_nb: %d, X: %#llx\n",
     params->width, params->precision, params->base, left_zeroes,
     right_spaces, left_spaces, char_nb, v);
#endif

  for (; left_spaces > 0; left_spaces--)
    {
      avprintf_output (CHAR_SPACE);
    }

  /* If we have a non-null value and if we were requested
   * the "alternate" form,
   */
  if (v && (params->flags & FLAG_ALTERNATE))
    {
      if (params->flags & FLAG_x_CNV)
        {
          avprintf_output (CHAR_0);
          avprintf_output (CHAR_x);
        }
      else if (params->flags & FLAG_X_CNV)
        {
          avprintf_output (CHAR_0);
          avprintf_output (CHAR_X);
        }
    }

  for (; left_zeroes > 0; left_zeroes--)
    {
      avprintf_output (CHAR_0);
    }

  for (char_nb--; char_nb >= 0; char_nb--)
    {
      unsigned char cur = v / local_power (params->base, char_nb);
      v -= cur * local_power (params->base, char_nb);

      if (cur < 10)
        {
          avprintf_output (CHAR_0 + cur);
        }
      else if (params->flags & FLAG_x_CNV)
        {
          avprintf_output (CHAR_a + cur - 10);
        }
      else if (params->flags & FLAG_X_CNV)
        {
          avprintf_output (CHAR_A + cur - 10);
        }
      else
        {
          /* should assert. */
        }
    }

  for (; right_spaces > 0; right_spaces--)
    {
      avprintf_output (CHAR_SPACE);
    }
}

static void
output_signed_int (avprintf_callback_t cb, void *context,
                   struct conversion_t const *params, signed long long v)
{
  if (v < 0)
    {
      v = -v;
      avprintf_output (CHAR_MINUS);
    }
  else if (params->flags & FLAG_PLUS)
    {
      avprintf_output (CHAR_PLUS);
    }
  else if (params->flags & FLAG_SPACE)
    {
      avprintf_output (CHAR_SPACE);
    }

  output_unsigned_int (cb, context, params, (unsigned long long) v);
}

static signed long long
read_signed_int (long flags, va_list * list)
{
  unsigned long long lls = 0;
  if (flags & FLAG_ll_LEN)
    {
      lls = va_arg (*list, signed long long int);
    }
  else if (flags & FLAG_l_LEN)
    {
      lls = va_arg (*list, signed long int);
    }
  else if (flags & FLAG_j_LEN)
    {
#ifdef HAVE_STDINT_H
      lls = va_arg (*list, intmax_t);
#endif
    }
  else if (flags & FLAG_z_LEN)
    {
#ifdef HAVE_STDDEF_H
      lls = va_arg (*list, ssize_t);
#endif
    }
  else if (flags & FLAG_t_LEN)
    {
#ifdef HAVE_STDDEF_H
      lls = va_arg (*list, ptrdiff_t);
#endif
    }
  else if (flags & FLAG_L_LEN)
    {
      /* ASSERT here. */
    }
  else if (flags & (FLAG_h_LEN | FLAG_hh_LEN))
    {
      lls = va_arg (*list, signed int);
    }
  else
    {
      lls = va_arg (*list, signed int);
    }
  return lls;
}

static unsigned long long
read_unsigned_int (long flags, va_list * list)
{
  unsigned long long llu = 0;
  if (flags & FLAG_ll_LEN)
    {
      llu = va_arg (*list, unsigned long long int);
    }
  else if (flags & FLAG_l_LEN)
    {
      llu = va_arg (*list, unsigned long int);
    }
  else if (flags & FLAG_j_LEN)
    {
#ifdef HAVE_STDINT_H
      llu = va_arg (*list, uintmax_t);
#endif
    }
  else if (flags & FLAG_z_LEN)
    {
#ifdef HAVE_STDDEF_H
      llu = va_arg (*list, size_t);
#endif
    }
  else if (flags & FLAG_t_LEN)
    {
#ifdef HAVE_STDDEF_H
      llu = va_arg (*list, uptrdiff_t);
#endif
    }
  else if (flags & FLAG_L_LEN)
    {
      /* ASSERT here. */
    }
  else if (flags & (FLAG_h_LEN | FLAG_hh_LEN))
    {
      llu = va_arg (*list, unsigned int);
    }
  else
    {
      llu = va_arg (*list, unsigned int);
    }

  return llu;
}

static unsigned long long
apply_unsigned_length_modifier (long flags, unsigned long long llu)
{
  if (flags & FLAG_h_LEN)
    {
      llu = (unsigned short int) llu;
    }
  else if (flags & FLAG_hh_LEN)
    {
      llu = (unsigned char) llu;
    }
  return llu;
}

static signed long long
apply_signed_length_modifier (long flags, signed long long llu)
{
  if (flags & FLAG_h_LEN)
    {
      llu = (signed short int) llu;
    }
  else if (flags & FLAG_hh_LEN)
    {
      llu = (signed char) llu;
    }
  return llu;
}

struct local_callback_t
{
  /* user-provided callback */
  avprintf_callback_t user_cb;
  /* user-provided context */
  void *user_context;
  /* number of characters written out since the start
     of the call to mvprintf_cb */
  long long signed int count;
};

static void
local_callback (char c, void *context)
{
  struct local_callback_t *cb = (struct local_callback_t *) context;
  cb->count++;
  cb->user_cb (c, cb->user_context);
}

int
avprintf_cb (avprintf_callback_t user_cb, void *user_context, char const *str,
             va_list alist)
{
  struct conversion_t params;
  enum parsing_state_t state = CONVERSION_SEEKING;
  va_list list;
  struct local_callback_t callback_context = {
    .user_cb = user_cb,
    .user_context = user_context,
    .count = 0
  };
  void *context = &callback_context;
  avprintf_callback_t cb = local_callback;

  va_copy (list, alist);

  if (str == NULL)
    {
      maprintf_cb (cb, context, "(null)\n");
      goto error;
    }

  while (*str != CHAR_NUL)
    {
      void *p;
      signed int *pn;
      char *s;
      unsigned long long llu;
      signed long long lls;
      signed int si;
      char c;
      if (state & CONVERSION_SEEKING)
        {
          if (*str == CHAR_PERCENT)
            {
              state = CONVERSION_PERCENT;
              params.precision = 1;
              params.width = 0;
              params.flags = 0;
            }
          else if (validate_ascii_7_bit (*str))
            {
              maprintf_cb (cb, context, "\n--ERROR-- non ascii string\n");
              goto error;
            }
          else
            {
              avprintf_output (*str);
            }
          str++;
        }
      else if (state & CONVERSION_NEXT)
        {
          switch (*str)
            {
            case CHAR_p:
            case CHAR_d:
            case CHAR_i:
            case CHAR_c:
            case CHAR_s:
            case CHAR_o:
            case CHAR_u:
            case CHAR_x:
            case CHAR_X:
            case CHAR_S:
            case CHAR_C:
            case CHAR_n:
              state = CONVERSION_FWPL;
              break;

            case CHAR_h:
            case CHAR_l:
            case CHAR_L:
            case CHAR_j:
            case CHAR_z:
            case CHAR_t:
            case CHAR_q:
            case CHAR_Z:
              state = CONVERSION_FWP;
              break;

            case CHAR_DOT:
              state = CONVERSION_FW;
              break;

            case CHAR_STAR:
            case CHAR_1:
            case CHAR_2:
            case CHAR_3:
            case CHAR_4:
            case CHAR_5:
            case CHAR_6:
            case CHAR_7:
            case CHAR_8:
            case CHAR_9:
              state = CONVERSION_F;
              break;
            default:
              maprintf_cb (cb, context,
                           "\n--ERROR-- Invalid conversion specifier.\n");
              goto error;
              break;
            }
        }
      else if (state & CONVERSION_PERCENT)
        {
          switch (*str)
            {
            case CHAR_PERCENT:
              /* percent character */
              avprintf_output (CHAR_PERCENT);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_POUND:
              params.flags |= FLAG_ALTERNATE;
              str++;
              break;
            case CHAR_0:
              params.flags |= FLAG_ZERO;
              str++;
              break;
            case CHAR_MINUS:
              params.flags |= FLAG_ADJUSTED;
              str++;
              break;
            case CHAR_SPACE:
              params.flags |= FLAG_SPACE;
              str++;
              break;
            case CHAR_PLUS:
              params.flags |= FLAG_PLUS;
              str++;
              break;
            default:
              state = CONVERSION_NEXT;
              break;
            }
        }
      else if (state & CONVERSION_F)
        {
          switch (*str)
            {
            case CHAR_STAR:
              si = va_arg (list, signed int);
              if (si < 0)
                {
                  params.flags |= FLAG_ADJUSTED;
                  params.width = -si;
                }
              else
                {
                  params.width = si;
                }
              str++;
              state = CONVERSION_FW;
              break;
            case CHAR_DOLLAR:
              /* end of width specification.
               * This is defined in SUS. We don't support it.
               */
              maprintf_cb (cb, context,
                           "\n--ERROR-- The SUS *m$-style width length not supported.\n");
              goto error;
              break;

            case CHAR_0:
            case CHAR_1:
            case CHAR_2:
            case CHAR_3:
            case CHAR_4:
            case CHAR_5:
            case CHAR_6:
            case CHAR_7:
            case CHAR_8:
            case CHAR_9:
              params.width *= 10;
              params.width += *str - CHAR_0;
              str++;
              break;
            default:
              state = CONVERSION_NEXT;
              break;
            }
        }
      else if (state & CONVERSION_FW)
        {
          switch (*str)
            {
            case CHAR_DOT:
              params.precision = 0;
              params.flags |= FLAG_PRECISION;
              str++;
              break;

            case CHAR_0:
            case CHAR_1:
            case CHAR_2:
            case CHAR_3:
            case CHAR_4:
            case CHAR_5:
            case CHAR_6:
            case CHAR_7:
            case CHAR_8:
            case CHAR_9:
              params.precision *= 10;
              params.precision += *str - CHAR_0;
              str++;
              break;
            case CHAR_STAR:
              si = va_arg (list, signed int);
              if (si < 0)
                {
                  /* as if the precision had never been specified at all */
                  params.precision = 1;
                  params.flags &= ~FLAG_PRECISION;
                }
              else
                {
                  params.precision = si;
                  params.flags |= FLAG_PRECISION;
                }
              str++;
              state = CONVERSION_FWP;
              break;
            case CHAR_DOLLAR:
              /* end of precision specification.
               * This is defined in SUS. We don't support it.
               */
              maprintf_cb (cb, context,
                           "\n--ERROR-- The SUS *m$-style precision length not supported.\n");
              break;
            default:
              state = CONVERSION_NEXT;
              break;
            }
        }
      else if (state & CONVERSION_FWP)
        {
          switch (*str)
            {
            case CHAR_h:
              state = CONVERSION_FWP_h;
              str++;
              break;
            case CHAR_l:
              state = CONVERSION_FWP_l;
              str++;
              break;
            case CHAR_L:
              maprintf_cb (cb, context,
                           "\n--ERROR--\"L\": floating-point support not implemented\n");
              //flags |= FLAG_L_LEN;
              //str++;
              goto error;
              break;
            case CHAR_j:
              params.flags |= FLAG_j_LEN;
              str++;
              break;
            case CHAR_z:
              params.flags |= FLAG_z_LEN;
              str++;
              break;
            case CHAR_t:
              params.flags |= FLAG_t_LEN;
              str++;
              break;
            case CHAR_q:
              maprintf_cb (cb, context,
                           "\n--ERROR--\"q\": unsupported length modifier (\"quad\" BSD 4.4 and libc5)\n");
              goto error;
              break;
            case CHAR_Z:
              params.flags |= FLAG_z_LEN;
              maprintf_cb (cb, context, "\n--ERROR-\"Z\": use z instead\n");
              str++;
              break;
            default:
              state = CONVERSION_NEXT;
              break;
            }
        }
      else if (state & CONVERSION_FWP_h)
        {
          if (*str == CHAR_h)
            {
              params.flags |= FLAG_hh_LEN;
              str++;
              state = CONVERSION_FWPL;
            }
          else
            {
              params.flags |= FLAG_h_LEN;
              state = CONVERSION_NEXT;
            }
        }
      else if (state & CONVERSION_FWP_l)
        {
          if (*str == CHAR_l)
            {
              params.flags |= FLAG_ll_LEN;
              str++;
              state = CONVERSION_FWPL;
            }
          else
            {
              params.flags |= FLAG_l_LEN;
              state = CONVERSION_NEXT;
            }
        }
      else if (state & CONVERSION_FWPL)
        {
          switch (*str)
            {
            case CHAR_p:
              /* pointer */
              p = va_arg (list, void *);
              if (p)
                {
                  params.flags |= FLAG_x_CNV;
                  params.flags |= FLAG_ALTERNATE;
                  params.base = 16;
                  output_unsigned_int (cb, context, &params,
                                       ((unsigned long) p));
                }
              else
                {
                  avprintf_output (CHAR_LPAREN);
                  avprintf_output (CHAR_n);
                  avprintf_output (CHAR_i);
                  avprintf_output (CHAR_l);
                  avprintf_output (CHAR_RPAREN);
                }
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_d:
            case CHAR_i:
              lls = read_signed_int (params.flags, &list);
              lls = apply_signed_length_modifier (params.flags, lls);
              params.base = 10;
              output_signed_int (cb, context, &params, lls);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_c:
              c = (char) va_arg (list, int);
              if (validate_ascii_7_bit (c))
                {
                  maprintf_cb (cb, context, "\n--ERROR-- non ascii string\n");
                  goto error;
                }
              avprintf_output ((unsigned char) c);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_s:
              s = (char *) va_arg (list, char *);
              while (*s != CHAR_NUL)
                {
                  if (validate_ascii_7_bit (*s))
                    {
                      maprintf_cb (cb, context,
                                   "\n--ERROR-- non ascii string\n");
                      goto error;
                    }
                  else if (params.flags & FLAG_PRECISION &&
                           (params.precision == 0))
                    {
                      break;
                    }
                  params.precision--;
                  avprintf_output (*s);
                  s++;
                }
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_o:
              llu = read_unsigned_int (params.flags, &list);
              llu = apply_unsigned_length_modifier (params.flags, llu);
              params.flags |= FLAG_o_CNV;
              params.base = 8;
              output_unsigned_int (cb, context, &params, llu);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_u:
              llu = read_unsigned_int (params.flags, &list);
              llu = apply_unsigned_length_modifier (params.flags, llu);
              params.base = 10;
              output_unsigned_int (cb, context, &params, llu);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_x:
              llu = read_unsigned_int (params.flags, &list);
              llu = apply_unsigned_length_modifier (params.flags, llu);
              params.flags |= FLAG_x_CNV;
              params.base = 16;
              output_unsigned_int (cb, context, &params, llu);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_X:
              llu = read_unsigned_int (params.flags, &list);
              llu = apply_unsigned_length_modifier (params.flags, llu);
              params.flags |= FLAG_X_CNV;
              params.base = 16;
              output_unsigned_int (cb, context, &params, llu);
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_S:
              maprintf_cb (cb, context,
                           "\n--ERROR--\"S\": Synonym for ls: do not use.\n");
              goto error;
              break;
            case CHAR_C:
              maprintf_cb (cb, context,
                           "\n--ERROR--\"C\": Synonym for lc: do not use.\n");
              goto error;
              break;
            case CHAR_n:
              pn = va_arg (list, signed int *);
              if (!pn)
                {
                  maprintf_cb (cb, context,
                               "\n--ERROR-- invalid NULL n pointer.\n");
                  goto error;
                }
              if (params.flags & FLAG_ll_LEN)
                {
                  *((long long signed int *) pn) = callback_context.count;
                }
              else if (params.flags & FLAG_l_LEN)
                {
                  *((long signed int *) pn) = callback_context.count;
                }
              else
                {
                  *((signed int *) pn) = callback_context.count;
                }
              state = CONVERSION_SEEKING;
              str++;
              break;
            case CHAR_e:
            case CHAR_E:
            case CHAR_f:
            case CHAR_F:
            case CHAR_g:
            case CHAR_G:
            case CHAR_a:
            case CHAR_A:
              maprintf_cb (cb, context,
                           "\n--ERROR-- floating-point conversion specifiers not implemented.\n");
              goto error;
              break;
            default:
              maprintf_cb (cb, context,
                           "\n--ERROR-- invalid conversion specifier.\n");
              goto error;
              break;
            }
        }
      else
        {
          goto error;
        }
    }

  avprintf_output (CHAR_NUL);

end:
  va_end (list);
  return callback_context.count;

error:
  maprintf_cb (cb, context, "Error during parsing.\n");
  goto end;
}

#ifdef RUN_SELF_TESTS

/*******************************************************************/
/***             Conformance Tests                               ***/
/*******************************************************************/

#ifndef TRUE
#define TRUE (1)
#endif
#ifndef FALSE
#define FALSE (0)
#endif

#define ARRAY_SIZE 1024
struct test_array_t
{
  char *cur;
  unsigned int left;
  int error;
  char start[ARRAY_SIZE + 1];
};

static void
marray_reset (struct test_array_t *array)
{
  array->start[ARRAY_SIZE] = CHAR_NUL;
  array->left = ARRAY_SIZE;
  array->cur = array->start;
  array->error = 0;
}

static void
marray_put (char c, void *context)
{
  struct test_array_t *array = (struct test_array_t *) context;
  if (array->left == 0)
    {
      mbootstrap_aprintf ("String too long for test: \"%s\"\n", array->start);
      array->error = TRUE;
      goto out;
    }

  *(array->cur) = c;
  (array->cur)++;
  array->left--;
out:
  return;
}


static int
mstrcmp (char const *a, char const *b)
{
  int retval = 0;
  while (*a != CHAR_NUL && *b != CHAR_NUL)
    {
      if (*a != *b)
        {
          retval = 1;
          goto out;
        }
      a++;
      b++;
    }
  if (*a != *b)
    {
      retval = 1;
    }
out:
  return retval;
}




#define TEST_PTR(expected, str, ptr)                  \
{                                                     \
        void *p = (void *)ptr;                        \
        maprintf_cb (marray_put, &array, str, p);     \
        if (array.error) {                            \
                failed = TRUE;                        \
        } else if (mstrcmp (expected, array.start)) { \
                failed = TRUE;                        \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\"(%p), got: \"%s\" with \"%s\"\n", \
                                         expected, p, array.start, str); \
        }                                             \
        marray_reset (&array);                        \
}

#define TEST_x(expected, str, x)                      \
{                                                     \
        maprintf_cb (marray_put, &array, str, x);     \
        if (array.error) {                            \
                failed = TRUE;                        \
        } else if (mstrcmp (expected, array.start)) { \
                failed = TRUE;                        \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\"(%x), got: \"%s\" with \"%s\"\n", \
                                         expected, x, array.start, str); \
        }                                             \
        marray_reset (&array);                        \
}

#define TEST_u(expected, str, u)                      \
{                                                     \
        maprintf_cb (marray_put, &array, str, u);     \
        if (array.error) {                            \
                failed = TRUE;                        \
        } else if (mstrcmp (expected, array.start)) { \
                failed = TRUE;                        \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\"(%u), got: \"%s\" with \"%s\"\n", \
                                         expected, u, array.start, str); \
        }                                             \
        marray_reset (&array);                        \
}

#define TEST_o(expected, str, o)                      \
{                                                     \
        maprintf_cb (marray_put, &array, str, o);     \
        if (array.error) {                            \
                failed = TRUE;                        \
        } else if (mstrcmp (expected, array.start)) { \
                failed = TRUE;                        \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\"(%o), got: \"%s\" with \"%s\"\n", \
                                         expected, o, array.start, str); \
        }                                             \
        marray_reset (&array);                        \
}

#define TEST_d(expected, str, d)                      \
{                                                     \
        maprintf_cb (marray_put, &array, str, d);     \
        if (array.error) {                            \
                failed = TRUE;                        \
        } else if (mstrcmp (expected, array.start)) { \
                failed = TRUE;                        \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\"(%d), got: \"%s\" with \"%s\"\n", \
                                         expected, d, array.start, str); \
        }                                             \
        marray_reset (&array);                        \
}

#define TEST_n(expected_str, expected_n, str)             \
{                                                         \
        signed int n;                                     \
        maprintf_cb (marray_put, &array, str, &n);        \
        if (array.error) {                                \
                failed = TRUE;                            \
        } else if (mstrcmp (expected_str, array.start) || \
                   expected_n != n) {                     \
                failed = TRUE;                            \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\"-%d, got: \"%s\"-%d with \"%s\"\n", \
                                         expected_str, expected_n, array.start, n, str); \
        }                                                 \
        marray_reset (&array);                            \
}


/* gcc feature: vararg macro. */
#define TEST_MULT(expected, str, ...)                          \
{                                                              \
        maprintf_cb (marray_put, &array, str, ## __VA_ARGS__); \
        if (array.error) {                                     \
                failed = TRUE;                                 \
        } else if (mstrcmp (expected, array.start)) {          \
                failed = TRUE;                                 \
                MTEST_BOOTSTRAP_APRINTF ("avprintf -- Expected: \"%s\", got: \"%s\" with \"%s\"\n", \
                                         expected, array.start, str); \
        }                                                      \
        marray_reset (&array);                                 \
}

int
test_avprintf (void)
{
  int failed = FALSE;

  /* NULL format string. */
  TEST_PTR ("(null)\n", NULL, 0);

  /* The C99 standard says pointer output is implementation-dependant.
   * I decided to follow the Glibc way: %p output is defined to be
   * equivalent to %#x output except for the NULL pointer which is
   * represented as nil.
   */

  /* normal pointer */
  TEST_PTR ("0xdeadbeaf", "%p", 0xdeadbeaf);
  TEST_PTR ("0xeadbeaf", "%p", 0xeadbeaf);
  TEST_PTR ("0xbeaf", "%p", 0xbeaf);
  TEST_PTR ("(nil)", "%p", NULL);
  /* width field */
  TEST_PTR ("_0x1_", "_%2p_", 0x1);
  TEST_PTR ("_0x1_", "_%3p_", 0x1);
  TEST_PTR ("_ 0x1_", "_%4p_", 0x1);
  TEST_PTR ("_       0x1_", "_%10p_", 0x1);
  /* width field and zero flag */
  TEST_PTR ("_0x1_", "_%02p_", 0x1);
  TEST_PTR ("_0x1_", "_%03p_", 0x1);
  TEST_PTR ("_0x01_", "_%04p_", 0x1);
  TEST_PTR ("_0x00000001_", "_%010p_", 0x1);
  /* width field and right-pad flag */
  TEST_PTR ("_0x1_", "_%-2p_", 0x1);
  TEST_PTR ("_0x1_", "_%-3p_", 0x1);
  TEST_PTR ("_0x1 _", "_%-4p_", 0x1);
  TEST_PTR ("_0x1       _", "_%-10p_", 0x1);
  /* width field and right-pad flag and zero flag (zero flag ignored) */
  TEST_PTR ("_0x1_", "_%0-2p_", 0x1);
  TEST_PTR ("_0x1_", "_%0-3p_", 0x1);
  TEST_PTR ("_0x1 _", "_%0-4p_", 0x1);
  TEST_PTR ("_0x1       _", "_%0-10p_", 0x1);
  TEST_PTR ("_0x1_", "_%-02p_", 0x1);
  TEST_PTR ("_0x1_", "_%-03p_", 0x1);
  TEST_PTR ("_0x1 _", "_%-04p_", 0x1);
  TEST_PTR ("_0x1       _", "_%-010p_", 0x1);
  /* precision field */
  TEST_PTR ("_0x1_", "_%.0p_", 0x1);
  TEST_PTR ("_0x1_", "_%.p_", 0x1);
  TEST_PTR ("_0x34_", "_%.1p_", 0x34);
  TEST_PTR ("_0x34_", "_%.2p_", 0x34);
  TEST_PTR ("_0x034_", "_%.3p_", 0x34);
  TEST_PTR ("_0x0034_", "_%.4p_", 0x34);
  TEST_PTR ("_0x00034_", "_%.5p_", 0x34);
  TEST_PTR ("_0x000034_", "_%.6p_", 0x34);
  /* precision field with width */
  TEST_PTR ("_0x0034_", "_%1.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%2.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%3.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%4.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%5.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%6.4p_", 0x34);
  TEST_PTR ("_ 0x0034_", "_%7.4p_", 0x34);
  TEST_PTR ("_      0x0034_", "_%12.4p_", 0x34);
  /* precision field with width with zero flag (zero flag ignored) */
  TEST_PTR ("_0x0034_", "_%01.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%02.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%03.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%04.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%05.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%06.4p_", 0x34);
  TEST_PTR ("_ 0x0034_", "_%07.4p_", 0x34);
  TEST_PTR ("_      0x0034_", "_%012.4p_", 0x34);
  /* precision field with width with right-pad flag */
  TEST_PTR ("_0x0034_", "_%-1.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-2.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-3.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-4.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-5.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-6.4p_", 0x34);
  TEST_PTR ("_0x0034 _", "_%-7.4p_", 0x34);
  TEST_PTR ("_0x0034      _", "_%-12.4p_", 0x34);
  /* precision field with width with right-pad
     flag with zero flag (zero flag ignored) */
  TEST_PTR ("_0x0034_", "_%-01.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-02.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-03.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-04.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-05.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%-06.4p_", 0x34);
  TEST_PTR ("_0x0034 _", "_%-07.4p_", 0x34);
  TEST_PTR ("_0x0034      _", "_%-012.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%0-1.4p_", 0x34);
  TEST_PTR ("_0x0034_", "_%0-6.4p_", 0x34);
  TEST_PTR ("_0x0034 _", "_%0-7.4p_", 0x34);
  TEST_PTR ("_0x0034      _", "_%0-12.4p_", 0x34);
  /* length modifiers (ignored on pointers) */
  TEST_PTR ("_0xdeadbeaf", "_%hhp", 0xdeadbeaf);
  TEST_PTR ("_0xdeadbeaf", "_%hp", 0xdeadbeaf);
  TEST_PTR ("_0xdeadbeaf", "_%lp", 0xdeadbeaf);
  TEST_PTR ("_0xdeadbeaf", "_%llp", 0xdeadbeaf);
  TEST_PTR ("_0xdeadbeaf", "_%jp", 0xdeadbeaf);
  TEST_PTR ("_0xdeadbeaf", "_%zp", 0xdeadbeaf);
  TEST_PTR ("_0xdeadbeaf", "_%tp", 0xdeadbeaf);
  /* Make sure multiple conversions can be done. */
  // XXX: this works only because int == ptr
  TEST_MULT ("_0x0034_0x00340x3434_", "_%-01.4p_%-01.4p%-01.4hhp_", 0x34,
             0x34, 0x3434);


  /* unsigned hexadecimal output */
  TEST_x ("0", "%x", 0);
  TEST_x ("beaf", "%x", 0xbeaf);
  TEST_x ("BEAF", "%X", 0xbeaf);
  TEST_x ("0xbeaf", "%#x", 0xbeaf);
  TEST_x ("0XBEAF", "%#X", 0xbeaf);
  /* width field */
  TEST_x ("_         1_", "_%10x_", 0x1);
  /* width field and zero flag */
  TEST_x ("_0000000001_", "_%010x_", 0x1);
  /* width field and right-pad flag */
  TEST_x ("_1         _", "_%-10x_", 0x1);
  /* width field and right-pad flag and zero flag (zero flag ignored) */
  TEST_x ("_1         _", "_%0-10x_", 0x1);
  TEST_x ("_1         _", "_%-010x_", 0x1);
  /* precision field */
  TEST_x ("__", "_%.0x_", 0x0);
  TEST_x ("__", "_%.x_", 0x0);
  TEST_x ("__", "_%.0X_", 0x0);
  TEST_x ("__", "_%.X_", 0x0);
  TEST_x ("_000034_", "_%.6x_", 0x34);
  /* precision field with width */
  TEST_x ("_        0034_", "_%12.4x_", 0x34);
  /* precision field with width with zero flag (zero flag ignored) */
  TEST_x ("_        0034_", "_%012.4x_", 0x34);
  /* precision field with width with right-pad flag */
  TEST_x ("_0034        _", "_%-12.4x_", 0x34);
  /* precision field with width with right-pad
     flag with zero flag (zero flag ignored) */
  TEST_x ("_0034        _", "_%-012.4x_", 0x34);
  TEST_x ("_0034        _", "_%0-12.4x_", 0x34);
  /* + flag (ignored) */
  TEST_x ("ea", "%+x", 0xea);
  /* space flag (ignored) */
  TEST_x ("ea", "% x", 0xea);
  TEST_x ("", "% .0x", 0);
  TEST_x ("0", "% .1x", 0);
  /* + and space flags (ignored) */
  TEST_x ("ea", "% +x", 0xea);
  /* length modifiers */
  TEST_x ("_af", "_%hhx", 0xdeadbeaf);
  TEST_x ("_beaf", "_%hx", 0xdeadbeaf);
  TEST_x ("_deadbeaf", "_%lx", 0xdeadbeaf);
  TEST_x ("_deadbeafdeadbeaf", "_%llx", 0xdeadbeafdeadbeaf);
  /* These are system-dependent...
     TEST_x ("_0xdeadbeaf", "_%jp", 0xdeadbeaf);
     TEST_x ("_0xdeadbeaf", "_%zp", 0xdeadbeaf);
     TEST_x ("_0xdeadbeaf", "_%tp", 0xdeadbeaf); */
  /* Make sure multiple conversions can be done. */
  TEST_MULT ("_0034_0034_0034_", "_%-01.4x_%0-1.4x_%-01.4hhx_", 0x34, 0x34,
             0x3434);


  /* unsigned decimal output */
  /* zero with zero precision leads to no character. */
  TEST_u ("", "%.0u", 0);
  /* normal decimal conversion. */
  TEST_u ("1", "%.0u", 1);
  TEST_u ("43", "%u", 43);
  TEST_u ("10", "%.0u", 10);
  TEST_u ("10", "%.2u", 10);
  TEST_u ("010", "%.3u", 10);
  TEST_u ("0000000010", "%.10u", 10);
  /* width parameter */
  TEST_u ("10", "%1u", 10);
  TEST_u ("10", "%2u", 10);
  TEST_u (" 10", "%3u", 10);
  TEST_u ("        10", "%10u", 10);
  /* width and zero flag. */
  TEST_u ("10", "%01u", 10);
  TEST_u ("10", "%02u", 10);
  TEST_u ("010", "%03u", 10);
  TEST_u ("0000000010", "%010u", 10);
  /* width and precision */
  TEST_u (" 10", "%3.2u", 10);
  TEST_u ("010", "%3.3u", 10);
  TEST_u (" 010", "%4.3u", 10);
  TEST_u ("0010", "%3.4u", 10);
  TEST_u ("  010", "%5.3u", 10);
  /* width and precision and zero flag */
  TEST_u (" 10", "%03.2u", 10);
  TEST_u ("010", "%03.3u", 10);
  TEST_u (" 010", "%04.3u", 10);
  TEST_u ("0010", "%03.4u", 10);
  TEST_u ("  010", "%05.3u", 10);
  /* width and right-padding */
  TEST_u ("10", "%-1u", 10);
  TEST_u ("10", "%-2u", 10);
  TEST_u ("10 ", "%-3u", 10);
  TEST_u ("10        ", "%-10u", 10);
  /* width and right-padding
     and zero flag (zero flag ignored) */
  TEST_u ("10", "%0-1u", 10);
  TEST_u ("10", "%-02u", 10);
  TEST_u ("10 ", "%0-3u", 10);
  TEST_u ("10        ", "%0-10u", 10);
  /* width and precision and right-padding */
  TEST_u ("10 ", "%-3.2u", 10);
  TEST_u ("010", "%-3.3u", 10);
  TEST_u ("010 ", "%-4.3u", 10);
  TEST_u ("0010", "%-3.4u", 10);
  TEST_u ("010  ", "%-5.3u", 10);
  TEST_u ("0255", "%-.4u", 0xff);
  /* width and precision and right-padding
     and zero flag (zero flag ignored) */
  TEST_u ("10 ", "%0-3.2u", 10);
  TEST_u ("010", "%-03.3u", 10);
  TEST_u ("010 ", "%0-4.3u", 10);
  TEST_u ("0010", "%-03.4u", 10);
  TEST_u ("010  ", "%0-5.3u", 10);
  /* space flag (ignored because not signed conversion) */
  TEST_u ("0", "% u", 0);
  TEST_u ("255", "% u", 0xff);
  TEST_u ("255", "% .0u", 0xff);
  TEST_u ("", "% .0u", 0);
  TEST_u ("1", "% u", 1);
  /* + flag (ignored because not signed conversion */
  TEST_u ("0", "%+u", 0);
  TEST_u ("1", "%+u", 1);
  /* + flag and space flag (still ignored) */
  TEST_u ("", "%+ .0u", 0);
  TEST_u ("0", "%+ .1u", 0);
  /* length modifiers. */
  TEST_u ("255", "%hhu", 0x1ffff);
  TEST_u ("0", "%hhu", 0x100);
  TEST_u ("65535", "%hu", 0x1ffff);
  TEST_u ("0", "%hu", 0x10000);
  /* length modifiers and width and precision zero and minus flags */
  TEST_u ("0255", "%04hhu", 0x1ffff);
  TEST_u ("0255", "%0.4hhu", 0x1ffff);
  TEST_u ("0255", "%-.4hhu", 0x1ffff);
  TEST_u ("0255", "%-0.4hhu", 0x1ffff);
  TEST_u ("0255 ", "%-5.4hhu", 0x1ffff);
  TEST_u ("255 ", "%-4hhu", 0x1ffff);
  /* add + flag to the mix */
  TEST_u ("0255", "%+04hhu", 0x1ffff);
  TEST_u ("0255", "%0+.4hhu", 0x1ffff);
  TEST_u ("0255", "%+-.4hhu", 0x1ffff);
  TEST_u ("0255 ", "%-+05.4hhu", 0x1ffff);


  /* unsigned octal output */
  TEST_o ("0", "%o", 0);
  TEST_o ("10", "%o", 8);
  TEST_o ("11", "%o", 9);
  /* space flag (ignored) */
  TEST_o ("11", "% o", 9);
  /* + flag (ignored) */
  TEST_o ("11", "%+o", 9);
  /* width */
  TEST_o ("        12", "%10o", 10);
  /* precision */
  TEST_o ("0000000012", "%.10o", 10);
  /* width and precision */
  TEST_o ("       012", "%10.3o", 10);
  /* width and precision and zero flag */
  TEST_o ("      012", "%09.3o", 10);
  /* width and zero flag */
  TEST_o ("000000012", "%09o", 10);
  /* width and precision and zero flag and - flag */
  TEST_o ("012      ", "%0-9.3o", 10);
  /* width and zero and - flags */
  TEST_o ("12       ", "%0-9o", 10);
  /* length specifier */
  TEST_o ("377", "%hho", 0x1ff);
  /* # flag */
  TEST_o ("", "%.0o", 0);
  TEST_o ("0", "%#.0o", 0);
  TEST_o ("0", "%#o", 0);
  TEST_o ("011", "%#o", 9);
  TEST_o ("0377", "%#hho", 0x1ff);
  TEST_o ("0377", "%#.4hho", 0x1ff);
  TEST_o ("00377", "%#.5hho", 0x1ff);
  TEST_o (" 00377", "%#06.5hho", 0x1ff);
  TEST_o ("000377", "%#06hho", 0x1ff);
  TEST_o ("  0377", "%#6hho", 0x1ff);
  TEST_o ("0377  ", "%#-6hho", 0x1ff);


  /* signed decimal output */
  TEST_d ("-10", "%d", -10);
  /* space flag ignored because leading character is a sign. */
  TEST_d ("-1", "% .0d", -1);
  /* length modifiers */
  TEST_d ("0", "%hhd", 0x100);
  TEST_d ("-1", "%hhd", 0xfffff);
  TEST_d ("-1", "%hhd", 0x1ffff);
  /* space flag */
  TEST_d (" 0", "% d", 0);
  TEST_d (" 255", "% d", 0xff);
  TEST_d (" 255", "% .0d", 0xff);
  TEST_d (" ", "% .0d", 0);
  TEST_d (" 1", "% d", 1);
  /* space flag ignored because + flag present */
  TEST_d ("+", "%+ .0d", 0);
  TEST_d ("+", "%+ .0d", 0);
  /* + flag and length modifier */
  TEST_d ("+", "%+ .0d", 0);


  /* string conversion. */
  TEST_MULT ("xxmat", "xx%.3s", "mathieu");
  TEST_MULT ("xx", "xx%.0s", "mathieu");
  TEST_MULT ("xx", "xx%.s", "mathieu");
  TEST_MULT ("xxmathieu", "xx%s", "mathieu");
  TEST_MULT ("xxmathieu", "xx%.7s", "mathieu");


  /* n conversion. That one is really evil */
  TEST_n ("", 0, "%n");
  TEST_n ("x", 1, "x%n");
  TEST_n ("x", 0, "%nx");
  TEST_n ("ggtt", 2, "gg%ntt");

  /* *-style width modifier. */
  TEST_MULT ("1", "%*x", 1, 1);
  TEST_MULT (" 1", "%*x", 2, 1);
  TEST_MULT ("         1", "%*x", 10, 1);
  TEST_MULT ("0000000001", "%0*x", 10, 1);
  TEST_MULT ("1         ", "%-*x", 10, 1);
  TEST_MULT ("1         ", "%-0*x", 10, 1);
  TEST_MULT ("1         ", "%*x", -10, 1);
  TEST_MULT ("          ", "%*.0x", -10, 0);
  /* *-style precision modifier. */
  TEST_MULT ("", "%.*x", 0, 0);
  TEST_MULT ("0", "%.*x", 1, 0);
  TEST_MULT ("00000", "%.*x", 5, 0);
  TEST_MULT ("00001", "%.*x", 5, 1);
  TEST_MULT ("1", "%.*x", -5, 1);
  TEST_MULT ("0", "%.*x", -5, 0);
  TEST_MULT ("0", "%.*x", -1, 0);
  /* *-style width and precision modifiers. */
  TEST_MULT ("1    ", "%*.*x", -5, -1, 1);
  TEST_MULT ("1    ", "%*.*x", -5, 1, 1);
  TEST_MULT ("01   ", "%*.*x", -5, 2, 1);
  TEST_MULT ("01   ", "%0*.*x", -5, 2, 1);
  TEST_MULT ("   01", "%0*.*x", 5, 2, 1);
  TEST_MULT ("   01", "%*.*x", 5, 2, 1);

  /* XXX: test return value. */

  return failed;
}

#endif /* RUN_SELF_TESTS */
