/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */

#ifndef AVPRINTF_CB_H
#define AVPRINTF_CB_H

#include <stdarg.h>

/****************************************************
 * This file implements the C99 standard-defined
 * C library printf function minus a few features:
 *
 *    - it does handle only ascii 7 bit. There is no
 *      wide character support. As such, the %S, %C,
 *      %ls and %lc conversions are not implemented.
 *
 *    - it does not handle float or double conversion
 *      I am open to partial implementations for this
 *      provided it keeps the code simple.
 *      As such, %aAeEfFgG are not implemented. The L
 *      length modifier is not implemented either.
 *
 *    - the non-C99 (SUS) *m$-style parameter setting
 *      is not implemented.
 *
 *    - the non-standard "q" (BSD4.4/libc5) length 
 *      modifier is not implemented.
 *
 *    - the non-standard "Z" (libc5) length modifier 
 *      is implemented but its use is discouraged. 
 *      Use "z" instead.
 *
 * This implementation uses a few compile-time 
 * switches implemented as macros:
 *    - HAVE_STDINT_H: if your compiler provides 
 *      stdint.h, define this macro. This will 
 *      enable the implementation of the "j" length
 *      modifiers.
 *    - HAVE_STDDEF_H: if your compiler provides
 *      stddef.h, define this macro. This will
 *      enable the implementation of the "z" and the
 *      "t" length modifiers.
 *    - ENABLE_FLOAT: enable single-precision float
 *      conversions. -- Not implemented.
 ***************************************************/

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*avprintf_callback_t) (char c, void *context);

int avprintf_cb (avprintf_callback_t callback, void *context, 
                 char const *str, va_list list);

int test_avprintf (void);

#ifdef __cplusplus
}
#endif


#endif /* AVPRINTF_CB_H */
