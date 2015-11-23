/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  pth_compat.c: Pth compatibility support
*/
                             /* ``Backward compatibility is for
                                  users who don't want to live
                                  on the bleeding edge.'' -- Unknown */
#include "pth_p.h"

COMPILER_HAPPYNESS(pth_compat)

/*
 *  Replacement for strerror(3)
 */

#if cpp
#if !defined(HAVE_STRERROR)
char *_pth_compat_strerror(int);
#define strerror(errnum) _pth_compat_strerror(errnum)
#endif
#endif

#if !defined(HAVE_STRERROR)
extern char *const sys_errlist[];
char *_pth_compat_strerror(int errnum)
{
    char *cp;

    cp = sys_errlist[errnum];
    return (cp);
}
#endif

