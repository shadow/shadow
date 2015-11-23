   ____  _   _
  |  _ \| |_| |__
  | |_) | __| '_ \                    ``Only those who attempt
  |  __/| |_| | | |                     the absurd can achieve
  |_|    \__|_| |_|                     the impossible.''

  GNU Pth - The GNU Portable Threads
  Version 2.0.7 (08-Jun-2006)

  ABSTRACT

  Pth is a very portable POSIX/ANSI-C based library for Unix platforms
  which provides non-preemptive priority-based scheduling for multiple
  threads of execution (aka `multithreading') inside event-driven
  applications. All threads run in the same address space of the server
  application, but each thread has its own individual program-counter,
  run-time stack, signal mask and errno variable.

  The thread scheduling itself is done in a cooperative way, i.e., the
  threads are managed by a priority- and event-based non-preemptive
  scheduler. The intention is, that this way one can achieve better
  portability and run-time performance than with preemptive scheduling.
  The event facility allows threads to wait until various types of
  events occur, including pending I/O on filedescriptors, asynchronous
  signals, elapsed timers, pending I/O on message ports, thread and
  process termination, and even customized callback functions.

  Additionally Pth provides an optional emulation API for POSIX.1c
  threads (`Pthreads') which can be used for backward compatibility to
  existing multithreaded applications.

  Finally, Pth guarranties its fixed set of API functionality on
  all platforms, i.e., functions like pth_poll(3), pth_readv(3) or
  pth_writev(3) are always available, even if the particular underlaying
  platform does not actually support their functionality (through the
  system calls poll(2), readv(2), writev(2), etc).

  Although Pth is very feature-rich, it is a rather small threading
  library. It consists only of approximately 7,000 line (or 300 KB) of
  ANSI C code which are auto-configured with approximately 400 lines (or
  60 KB) of Autoconf/m4 macros and which are documented by approximately
  3,000 lines (or 150 KB) of documentation. Additionally the sources
  are documented with approximately 3,600 additional lines of comments.
  As a result, the whole source tree is just about 1.5 MB in size and
  fits into a small tarball less than 350 KB in size. This allows Pth to
  fit very well even into the source tree of other applications without
  bloating it up very much.

  Pth was successfully tested on FreeBSD, NetBSD, OpenBSD, BSDI,
  GNU/Linux, Solaris, HPUX, Tru64 (OSF/1), AIX, IRIX, UnixWare, SCO
  OpenServer, SINIX, ReliantUNIX, ISC, AmigaOS, Rhapsody (MacOS X), FTX,
  AUX and Win32/Cygwin. And it should should automatically adjust itself
  to remaining Unix platforms, too.

  COPYRIGHT AND LICENSE

  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library (see file COPYING); if not, write
  to the Free Software Foundation, Inc., 59 Temple Place, Suite
  330, Boston, MA 02111-1307 USA, or contact Ralf S. Engelschall
  <rse@engelschall.com>.

  HOME AND DOCUMENTATION

  The documentation and latest release can be found on

  o OSSP: http://www.ossp.org/pkg/lib/pth/
  o OSSP:  ftp://ftp.ossp.org/pkg/lib/pth/
  o GNU:  http://www.gnu.org/software/pth/
  o GNU:   ftp://ftp.gnu.org/gnu/pth/

                                       Ralf S. Engelschall
                                       rse@engelschall.com
                                       www.engelschall.com
