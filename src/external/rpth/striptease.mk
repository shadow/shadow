##
##  GNU Pth - The GNU Portable Threads
##  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
##
##  This file is part of GNU Pth, a non-preemptive thread scheduling
##  library which can be found at http://www.gnu.org/software/pth/.
##
##  This library is free software; you can redistribute it and/or
##  modify it under the terms of the GNU Lesser General Public
##  License as published by the Free Software Foundation; either
##  version 2.1 of the License, or (at your option) any later version.
##
##  This library is distributed in the hope that it will be useful,
##  but WITHOUT ANY WARRANTY; without even the implied warranty of
##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
##  Lesser General Public License for more details.
##
##  You should have received a copy of the GNU Lesser General Public
##  License along with this library; if not, write to the Free Software
##  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
##  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
##
##  striptease.mk: Pth Makefile input for stripped down version
##
                              # ``The "micro" in "microkernel" was
                              #   originally intended to mean 'small':
                              #   Believe it or not.'' -- Ripley
@SET_MAKE@

CC          = @CC@
CPPFLAGS    = @CPPFLAGS@ -I.
CFLAGS      = @CFLAGS@
LDFLAGS     = @LDFLAGS@ -L.
LIBS        = @LIBS@
AR          = @AR@
RANLIB      = @RANLIB@
SHTOOL      = ./shtool
RM          = rm -f

LIBS        = libpth.a @LIBPTHREAD_A@
OBJS        = pth.o pth_vers.o @PTHREAD_O@
SRCS        = pth.c pth_vers.c

all: pth_p.h $(LIBS)

.SUFFIXES:
.SUFFIXES: .c .o .lo
.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $<

pth_p.h: pth_p.h.in
	$(SHTOOL) scpp -o pth_p.h -t pth_p.h.in -Dcpp -Cintern -M '==#==' $(SRCS)

libpth.a: pth.o pth_vers.o
	$(AR) rc libpth.a pth.o pth_vers.o
	$(RANLIB) libpth.a

libpthread.a: pth.o pth_vers.o pthread.o
	$(AR) rc libpthread.a pth.o pth_vers.o pthread.o
	$(RANLIB) libpthread.a

clean:
	$(RM) $(LIBS)
	$(RM) $(OBJS)

distclean: clean
	$(RM) config.cache config.log config.status
	$(RM) pth_p.h pth.h pthread.h pth_acdef.h pth_acmac.h
	$(RM) Makefile

test:
check:
install:

