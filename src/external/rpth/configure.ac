dnl ##
dnl ##  GNU Pth - The GNU Portable Threads
dnl ##  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
dnl ##
dnl ##  This file is part of GNU Pth, a non-preemptive thread scheduling
dnl ##  library which can be found at http://www.gnu.org/software/pth/.
dnl ##
dnl ##  This library is free software; you can redistribute it and/or
dnl ##  modify it under the terms of the GNU Lesser General Public
dnl ##  License as published by the Free Software Foundation; either
dnl ##  version 2.1 of the License, or (at your option) any later version.
dnl ##
dnl ##  This library is distributed in the hope that it will be useful,
dnl ##  but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl ##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
dnl ##  Lesser General Public License for more details.
dnl ##
dnl ##  You should have received a copy of the GNU Lesser General Public
dnl ##  License along with this library; if not, write to the Free Software
dnl ##  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
dnl ##  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
dnl ##
dnl ##  configure.ac: Pth Autoconf specification
dnl ##

dnl #                              ``Programming in Bourne-Shell
dnl #                                is a higher form of masochism.''
dnl #                                           -- Unknown

dnl ##
dnl ##  PROLOG
dnl ##

dnl #   standard Autoconf prolog
AC_PREREQ(2.52)dnl
AC_REVISION([1.0])

dnl #   autoconf initialization
AC_INIT(README)
AC_ENABLESUBDIR
AC_SRCDIR_PREFIX(srcdir_prefix)
AC_HEADLINE(dnl
GNU Pth, Portable Threads, dnl
PTH_VERSION, pth_vers.c, dnl
[Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>])
AC_CONFIG_PARAM(config.param)
AC_CONFIG_HEADER(pth_acdef.h)dnl
AC_PREFIX_DEFAULT(/usr/local)
AC_PLATFORM(PLATFORM)

dnl ##
dnl ##  BUILD TOOLS
dnl ##

AC_MSG_PART(Build Tools)

AC_PROG_CC
AC_PROG_CPP
AC_PROG_EGREP
AC_SET_MAKE
AC_CHECK_DEBUGGING
AC_CHECK_PROFILING
AC_CHECK_OPTIMIZE
if test -f "$srcdir/ltmain.sh"; then
    case "$PLATFORM" in
        #   Solaris 2.7/x86 is slightly broken
        *-pc-solaris2.[[78]] [)] enable_shared=no ;;
    esac
    sinclude(libtool.m4)
    AC_PROG_LIBTOOL
else
    dnl # only for stripped down Pth source tree
    AC_CHECK_PROG(AR, ar, ar)
    AC_PROG_RANLIB
fi

dnl ##  Support for some special platform/compiler options
case "$PLATFORM:$CC" in
    *-sun-solaris2*:cc )
        #   shut-up harmless warnings caused by do-while macros on Solaris
        test ".$ac_cv_prog_gcc" = ".no" && CFLAGS="$CFLAGS -w"
        ;;
    *-*-sysv*uw[[27]]*:cc )
        #   shut-up harmless warnings caused by do-while macros on UnixWare
        CFLAGS="$CFLAGS -w"
        ;;
    *-hp-hpux*:cc )
        #   HPUX needs a few special options to find its ANSI C brain
        CFLAGS="$CFLAGS -Ae -O"
        ;;
    *-hp-hpux*:CC )
        #   HPUX outputs useless warnings
        CFLAGS="$CFLAGS -w"
        ;;
    *-sgi-irix6.2:cc )
        #   shut-up warnings caused by IRIX brain-dead 32/64 bit stuff
        LDFLAGS="$LDFLAGS -woff 85 -Wl,-woff,84 -Wl,-woff,85"
        ;;
    *-sgi-irix6.5.[[2-9]]:cc )
        #   since IRIX 6.5.2 the C compiler is smart enough,
        #   but the linker is still complaining, of course
        LDFLAGS="$LDFLAGS -woff 84,85,134"
        ;;
    *-sgi-irix6.5:cc )
        #   shut-up warnings caused by IRIX brain-dead 32/64 bit stuff
        CFLAGS="$CFLAGS -woff 1110,1048"
        LDFLAGS="$LDFLAGS -woff 84,85,134"
        ;;
    *-dec-osf4*:cc )
        #   make sure the standard compliant functions are used on Tru64/OSF1 4.x
        CFLAGS="$CFLAGS -std"
        ;;
    *-dec-osf5*:cc )
        #   make sure the standard compliant functions are used on Tru64/OSF1 5.x
        CFLAGS="$CFLAGS -std -D_XOPEN_SOURCE_EXTENDED"
        ;;
    *-*-isc*:* )
        #   ISC is brain-dead and needs a bunch of options to find its brain
        CPPFLAGS="$CPPFLAGS -D_POSIX_SOURCE";
        LIBS="$LIBS -lcposix -linet";
        ;;
    *-*-aix4.1*:cc )
        #   AIX 4.1.x' cc complains about our use of "volatile"
        CPPFLAGS="$CPPFLAGS '-Dvolatile=/**/'";
        ;;
esac

dnl ##
dnl ##   MANDATORY PLATFORM ENVIRONMENT
dnl ##

AC_MSG_PART(Mandatory Platform Environment)

dnl # check for standard headers
AC_HEADER_STDC
AC_HAVE_HEADERS(dnl
   stdio.h stdlib.h stdarg.h string.h signal.h unistd.h setjmp.h fcntl.h dnl
   errno.h sys/types.h sys/time.h sys/wait.h sys/stat.h sys/socket.h)
AC_CHECK_FUNCS(dnl
   gettimeofday select sigaction sigprocmask sigpending sigsuspend)
AC_BEGIN_DECISION([mandatory system headers and functions])
AC_IFALLYES(dnl
   header:stdio.h header:stdlib.h header:stdarg.h header:string.h dnl
   header:signal.h header:unistd.h header:setjmp.h header:fcntl.h header:errno.h dnl
   header:sys/types.h header:sys/time.h header:sys/wait.h header:sys/stat.h header:sys/socket.h dnl
   func:gettimeofday func:select func:sigaction func:sigprocmask dnl
   func:sigpending func:sigsuspend,
   AC_DECIDE(fine, [all fine]))
AC_END_DECISION


dnl ##
dnl ##  OPTIONAL PLATFORM ENVIRONMENT
dnl ##

AC_MSG_PART(Optional Platform Environment)

dnl #  check for the number of signals
AC_CHECK_NSIG(PTH_NSIG)

dnl #  check for size of fd_set (FDSETSIZE)
AC_CHECK_FDSETSIZE(PTH_FDSETSIZE)

dnl # check whether poll(2)'s input stuff has to be faked
AC_CHECK_FUNCS(poll)
AC_CHECK_DEFINE(POLLIN, poll.h)
AC_MSG_CHECKING(whether poll(2) facility has to be faked)
AC_IFALLYES(func:poll define:POLLIN, PTH_FAKE_POLL=0, PTH_FAKE_POLL=1)
if test .$PTH_FAKE_POLL = .1; then
    msg="yes"
else
    msg="no"
fi
AC_SUBST(PTH_FAKE_POLL)
AC_MSG_RESULT([$msg])

dnl # check for readv/writev environment
AC_HAVE_HEADERS(sys/uio.h)
AC_CHECK_FUNCS(readv writev)
AC_MSG_CHECKING(whether readv(2)/writev(2) facility has to be faked)
AC_IFALLYES(func:readv func:writev header:sys/uio.h, PTH_FAKE_RWV=0, PTH_FAKE_RWV=1)
if test .$PTH_FAKE_RWV = .1; then
    msg="yes"
else
    msg="no"
fi
AC_SUBST(PTH_FAKE_RWV)
AC_MSG_RESULT([$msg])

dnl # check for various other functions which would be nice to have
AC_CHECK_FUNCS(usleep strerror)

dnl # check for various other headers which we might need
AC_HAVE_HEADERS(sys/resource.h net/errno.h paths.h)

dnl # at least the test programs need some socket stuff
AC_CHECK_LIB(nsl, gethostname)
if test ".`echo $LIBS | grep nsl`" = .; then
    AC_CHECK_LIB(nsl, gethostbyname)
fi
AC_CHECK_LIB(socket, accept)

dnl # check whether we've to use a non-standard #include <sys/select.h>  to get
dnl # the definition for fd_set under AIX and other brain-dead platforms.
AC_HAVE_HEADERS(sys/select.h)
EXTRA_INCLUDE_SYS_SELECT_H="#include <sys/select.h>"
if test ".$ac_cv_header_sys_select_h" != .yes; then
    EXTRA_INCLUDE_SYS_SELECT_H="/* $EXTRA_INCLUDE_SYS_SELECT_H */"
fi
AC_SUBST(EXTRA_INCLUDE_SYS_SELECT_H)

dnl # check whether we've to define sig_atomic_t
AC_CHECK_TYPEDEF(sig_atomic_t, signal.h)
FALLBACK_SIG_ATOMIC_T="typedef int sig_atomic_t;"
if test ".$ac_cv_typedef_sig_atomic_t" = .yes; then
    FALLBACK_SIG_ATOMIC_T="/* $FALLBACK_SIG_ATOMIC_T */"
fi
AC_SUBST(FALLBACK_SIG_ATOMIC_T)

dnl # check whether we've to define pid_t
AC_CHECK_TYPEDEF(pid_t, sys/types.h)
FALLBACK_PID_T="typedef int pid_t;"
if test ".$ac_cv_typedef_pid_t" = .yes; then
    FALLBACK_PID_T="/* $FALLBACK_PID_T */"
fi
AC_SUBST(FALLBACK_PID_T)

dnl # check whether we've to define size_t
AC_CHECK_TYPEDEF(size_t, stdlib.h)
FALLBACK_SIZE_T="typedef unsigned int size_t;"
if test ".$ac_cv_typedef_size_t" = .yes; then
    FALLBACK_SIZE_T="/* $FALLBACK_SIZE_T */"
fi
AC_SUBST(FALLBACK_SIZE_T)

dnl # check whether we've to define ssize_t
AC_CHECK_TYPEDEF(ssize_t, sys/types.h)
FALLBACK_SSIZE_T="typedef unsigned int ssize_t;"
if test ".$ac_cv_typedef_ssize_t" = .yes; then
    FALLBACK_SSIZE_T="/* $FALLBACK_SSIZE_T */"
fi
AC_SUBST(FALLBACK_SSIZE_T)

dnl # check whether we've to define off_t
AC_CHECK_TYPEDEF(off_t, sys/types.h)
FALLBACK_OFF_T="typedef int off_t;"
if test ".$ac_cv_typedef_off_t" = .yes; then
    FALLBACK_OFF_T="/* $FALLBACK_OFF_T */"
fi
AC_SUBST(FALLBACK_OFF_T)

dnl # check whether stack_t exists instead of sigaltstack
AC_CHECK_TYPEDEF(stack_t, signal.h)

dnl # check whether ss_base instead of ss_sp attribute exists
AC_CHECK_STRUCTATTR(ss_base, sigaltstack, sys/signal.h)
AC_CHECK_STRUCTATTR(ss_sp,   sigaltstack, sys/signal.h)

dnl # check for gettimeofday() variant
AC_MSG_CHECKING(for a single-argument based gettimeofday)
cross_compile=no
AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
],[
struct timeval tv;
(void)gettimeofday(&tv);
],
msg="yes"
,
msg="no"
)
case $PLATFORM in
    *-*-aix4* ) msg="no" ;; # on AIX the compiler test doesn't work
    *-*-isc* )  msg="no" ;; # on ISC the compiler test doesn't work
esac
if test ".$msg" = .yes; then
    AC_DEFINE(HAVE_GETTIMEOFDAY_ARGS1, 1, [define if gettimeofday(2) wants a single-argument only])
fi
AC_MSG_RESULT([$msg])

dnl # check for struct timespec
AC_MSG_CHECKING(for struct timespec)
cross_compile=no
AC_TRY_COMPILE([
#include <time.h>
#include <sys/time.h>
],[
struct timespec ts;
],
msg="yes"
,
msg="no"
)
if test ".$msg" = .yes; then
    AC_DEFINE(HAVE_STRUCT_TIMESPEC, 1, [define if exists "struct timespec"])
fi
AC_MSG_RESULT([$msg])

dnl # check for network/socket size type
AC_CHECK_SOCKLENTYPE([
FALLBACK_SOCKLEN_T="typedef ${ac_type} socklen_t;"
if test ".$ac_type" = ".socklen_t"; then
    FALLBACK_SOCKLEN_T="/* $FALLBACK_SOCKLEN_T */"
fi
AC_SUBST(FALLBACK_SOCKLEN_T)
])

dnl # check for filedescriptor number type
AC_CHECK_NFDSTYPE([
FALLBACK_NFDS_T="typedef ${ac_type} nfds_t;"
if test ".$ac_type" = ".nfds_t"; then
    FALLBACK_NFDS_T="/* $FALLBACK_NFDS_T */"
fi
AC_SUBST(FALLBACK_NFDS_T)
])

dnl # check for 64-bit types (for pth_snprintf.c)
AC_CHECK_LONGLONG
AC_CHECK_LONGDOUBLE

dnl ##
dnl ##  MACHINE CONTEXT IMPLEMENTATION
dnl ##

AC_MSG_PART(Machine Context Implementation)

dnl #
dnl #  1. determine possibilities
dnl #

dnl #  check for MCSC method
AC_CHECK_HEADER(ucontext.h,,, [#include <sys/types.h>])
AC_CHECK_FUNCS(makecontext swapcontext getcontext setcontext)
AC_CHECK_MCSC(mcsc=yes, mcsc=no)

dnl #  check for SJLJ method
AC_CHECK_HEADERS(signal.h)
AC_CHECK_FUNCS(sigsetjmp siglongjmp setjmp longjmp _setjmp _longjmp)
AC_CHECK_FUNCS(sigaltstack sigstack)
AC_CHECK_SJLJ(sjlj=yes, sjlj=no, sjlj_type)

dnl #
dnl #  2. make a general decision
dnl #

if test ".$mcsc" = .yes; then
    mctx_mth=mcsc
    mctx_dsp=sc
    mctx_stk=mc
elif test ".$sjlj" = .yes; then
    mctx_mth=sjlj
    mctx_dsp=$sjlj_type
    mctx_stk=none
    AC_IFALLYES(func:sigstack,    mctx_stk=ss)
    AC_IFALLYES(func:sigaltstack, mctx_stk=sas)
    case $mctx_dsp in
         sjljlx|sjljisc|sjljw32 ) mctx_stk=none
    esac
else
    AC_ERROR([no appropriate mctx method found])
fi

dnl #
dnl #  3. allow decision to be overridden by user
dnl #

AC_ARG_WITH(mctx-mth,dnl
[  --with-mctx-mth=ID      force mctx method      (mcsc,sjlj)],[
case $withval in
    mcsc|sjlj ) mctx_mth=$withval ;;
    * ) AC_ERROR([invalid mctx method -- allowed: mcsc,sjlj]) ;;
esac
])dnl
AC_ARG_WITH(mctx-dsp,dnl
[  --with-mctx-dsp=ID      force mctx dispatching (sc,ssjlj,sjlj,usjlj,sjlje,...)],[
case $withval in
    sc|ssjlj|sjlj|usjlj|sjlje|sjljlx|sjljisc|sjljw32 ) mctx_dsp=$withval ;;
    * ) AC_ERROR([invalid mctx dispatching -- allowed: sc,ssjlj,sjlj,usjlj,sjlje,sjljlx,sjljisc,sjljw32]) ;;
esac
])dnl
AC_ARG_WITH(mctx-stk,dnl
[  --with-mctx-stk=ID      force mctx stack setup (mc,ss,sas,...)],[
case $withval in
    mc|ss|sas|none ) mctx_stk=$withval ;;
    * ) AC_ERROR([invalid mctx stack setup -- allowed: mc,ss,sas,none]) ;;
esac
])dnl

dnl #
dnl #  4. determine a few additional details
dnl #

dnl #  whether sigaltstack has to use stack_t instead of struct sigaltstack
AC_CHECK_TYPEDEF(stack_t, signal.h)

dnl #  direction of stack grow
AC_CHECK_STACKGROWTH(PTH_STACKGROWTH)
if test ".$ac_cv_check_stackgrowth" = ".down"; then
    PTH_STACK_GROWTH="down"
else
    PTH_STACK_GROWTH="up"
fi
AC_SUBST(PTH_STACK_GROWTH)

dnl #  GNU Hurd has a broken libc which has stubs for sigaltstack and
dnl #  sigstack. When the following Autoconf checks for them are run, it
dnl #  even crashes the system. So we have to disable these checks here.
case $PLATFORM in
    *86-*-gnu0* ) ac_cv_func_sigaltstack="no"; ac_cv_func_sigstack="no" ;;
esac

dnl #  how to specify stacks for the various functions
AC_CHECK_STACKSETUP(makecontext, pth_skaddr_makecontext, pth_sksize_makecontext)
AC_CHECK_STACKSETUP(sigaltstack, pth_skaddr_sigaltstack, pth_sksize_sigaltstack)
AC_CHECK_STACKSETUP(sigstack,    pth_skaddr_sigstack,    pth_sksize_sigstack)

dnl #  how to implement POSIX compliant sig{set,long}jmp()
case $mctx_dsp in [
    ssjlj )
        pth_sigjmpbuf='sigjmp_buf'
        pth_sigsetjmp='sigsetjmp(buf,1)'
        pth_siglongjmp='siglongjmp(buf,val)'
        ;;
    sjlj )
        pth_sigjmpbuf='jmp_buf'
        pth_sigsetjmp='setjmp(buf)'
        pth_siglongjmp='longjmp(buf,val)'
        ;;
    usjlj )
        pth_sigjmpbuf='jmp_buf'
        pth_sigsetjmp='_setjmp(buf)'
        pth_siglongjmp='_longjmp(buf,val)'
        ;;
    sjlje )
        pth_sigjmpbuf='jmp_buf'
        pth_sigsetjmp='setjmp(buf)'
        pth_siglongjmp='longjmp(buf,val)'
        ;;
    sjljlx|sjljisc|sjljw32 )
        pth_sigjmpbuf='sigjmp_buf'
        pth_sigsetjmp='sigsetjmp(buf,1)'
        pth_siglongjmp='siglongjmp(buf,val)'
        ;;
] esac
pth_sigjmpbuf="#define pth_sigjmpbuf $pth_sigjmpbuf"
pth_sigsetjmp="#define pth_sigsetjmp(buf) $pth_sigsetjmp"
pth_siglongjmp="#define pth_siglongjmp(buf,val) $pth_siglongjmp"
AC_SUBST(pth_sigjmpbuf)
AC_SUBST(pth_sigsetjmp)
AC_SUBST(pth_siglongjmp)

dnl #
dnl #  5. export the results
dnl #

AC_DEFINE_UNQUOTED(PTH_MCTX_MTH_use, [PTH_MCTX_MTH_$mctx_mth], [define for machine context method])
AC_DEFINE_UNQUOTED(PTH_MCTX_DSP_use, [PTH_MCTX_DSP_$mctx_dsp], [define for machine context dispatching])
AC_DEFINE_UNQUOTED(PTH_MCTX_STK_use, [PTH_MCTX_STK_$mctx_stk], [define for machine context stack])

PTH_MCTX_ID="$mctx_mth/$mctx_dsp/$mctx_stk"
AC_MSG_RESULT([decision on mctx implementation... ${TB}${PTH_MCTX_ID}${TN}])
AC_SUBST(PTH_MCTX_ID)

AC_MSG_VERBOSE([decided mctx method: $mctx_mth])
AC_MSG_VERBOSE([decided mctx dispatching: $mctx_dsp])
AC_MSG_VERBOSE([decided mctx stack setup: $mctx_stk])


dnl ##
dnl ##  SYSTEM CALL MAPPING
dnl ##

AC_MSG_PART(System Call Mapping)

dnl #   check for syscall(2)
AC_CHECK_HEADERS(sys/syscall.h sys/socketcall.h)
AC_CHECK_FUNCS(syscall)
AC_CHECK_DEFINE(SYS_read, sys/syscall.h)

dnl #   check for dlsym(3) with RTLD_NEXT
AC_CHECK_HEADERS(dlfcn.h)
AC_CHECK_LIB(dl, dlsym)
AC_CHECK_FUNCS(dlopen dlsym dlclose)
AC_CHECK_DEFINE(RTLD_NEXT, dlfcn.h)

dnl #   check for path to dynamic C library
AC_MSG_CHECKING([for syscall dynamic libraries])
pth_syscall_libs=""
dirs=`echo "$LDFLAGS" |\
      sed -e 's;-L\([[^ ]]*\);<\1>;g' |\
      sed -e 's;^[[^<]]*<;;' -e 's;>[[^<]]*<; ;g' -e 's;>.*$;;'`
dirs="$dirs /usr/lib /lib"
libs=`echo "$LIBS" |\
      sed -e 's;-l\([[^ ]]*\);<\1>;g' |\
      sed -e 's;^[[^<]]*<;;' -e 's;>[[^<]]*<; ;g' -e 's;>.*$;;'`
libs="$libs c"
for lib in $libs; do
    for dir in $dirs; do
        for ext in so sl; do
            if test -f "${dir}/lib${lib}.${ext}"; then
                if test ".$pth_syscall_libs" = .; then
                    pth_syscall_libs="${dir}/lib${lib}.${ext}"
                else
                    pth_syscall_libs="${pth_syscall_libs}:${dir}/lib${lib}.${ext}"
                fi
            fi
        done
    done
done
AC_MSG_RESULT([$pth_syscall_libs])
AC_DEFINE_UNQUOTED(PTH_SYSCALL_LIBS, 
                   ["$pth_syscall_libs"],
                   [define for the paths to syscall dynamic libraries])

AC_MSG_CHECKING([whether soft system call mapping is used])
AC_ARG_ENABLE(syscall-soft,dnl
[  --enable-syscall-soft   enable soft system call mapping (default=no)],
enable_syscall_soft="$enableval",
if test ".$enable_syscall_soft" = .; then
    enable_syscall_soft=no
fi
)dnl
if test .$enable_syscall_soft = .yes; then
    PTH_SYSCALL_SOFT=1
    msg="yes"
else
    PTH_SYSCALL_SOFT=0
    msg="no"
fi
AC_MSG_RESULT([$msg])
AC_SUBST(PTH_SYSCALL_SOFT)

AC_MSG_CHECKING([whether hard system call mapping is used])
AC_ARG_ENABLE(syscall-hard,dnl
[  --enable-syscall-hard   enable hard system call mapping (default=no)],
enable_syscall_hard="$enableval",[
if test ".$enable_syscall_hard" = .; then
    enable_syscall_hard=no
fi
])dnl
if test ".$enable_syscall_hard" = .yes; then
    AC_IFALLYES(func:syscall header:sys/syscall.h define:SYS_read,
                enable_syscall_hard=yes, enable_syscall_hard=no)
fi
if test ".$enable_syscall_hard" = .yes; then
    PTH_SYSCALL_HARD=1
    msg="yes"
else
    PTH_SYSCALL_HARD=0
    msg="no"
fi
AC_MSG_RESULT([$msg])
AC_SUBST(PTH_SYSCALL_HARD)
if test ".$enable_syscall_hard" = .yes; then
    echo "${TB}Warning: The hard system call mapping variant is an experimental feature.${TN}"
    echo "${TB}         It is not guaranteed to work if the mapped system calls on your${TN}"
    echo "${TB}         platform do not 100% comply to the POSIX.1-2001/SUSv3 prototypes.${TN}"
fi


dnl ##
dnl ##  BUILD PARAMETERS
dnl ##

AC_MSG_PART(Build Parameters)

dnl #  check for various build parameters
AC_CHECK_BUILDPARAM

dnl #  whether to build Pthread library
AC_MSG_CHECKING(whether to build Pthread library)
AC_ARG_ENABLE(pthread,dnl
[  --enable-pthread        build Pthread library (default=no)],
enable_pthread="$enableval",
if test ".$enable_pthread" = .; then
    enable_pthread=no
fi
)dnl
AC_MSG_RESULT([$enable_pthread])
if test ".$enable_pthread" = .yes; then
    PTHREAD_O=pthread.o       dnl # only for stripped down Pth source tree
    LIBPTHREAD_A=libpthread.a dnl # only for stripped down Pth source tree
    LIBPTHREAD_LA=libpthread.la
    PTHREAD_CONFIG_1='$(S)pthread-config.1'
    PTHREAD_3='$(S)pthread.3'
    INSTALL_PTHREAD=install-pthread
    UNINSTALL_PTHREAD=uninstall-pthread
    TEST_PTHREAD=test_pthread
    enable_syscall_soft=yes
    PTH_SYSCALL_SOFT=1
    AC_HAVE_HEADERS(pthread.h)
    ac_warn=no
    if test ".$ac_cv_header_pthread_h" = .yes; then
        warn=yes
        changequote(<<,>>)
        case $PLATFORM in
            *-*-freebsd* ) ac_warn=no ;;
            *-*-linux*   ) ac_warn=no ;;
            *-*-solaris* ) ac_warn=no ;;
            *-*-irix*    ) ac_warn=no ;;
        esac
        changequote([,])
    fi
    if test ".$ac_warn" = .yes; then
        echo "${TB}Warning: You seem to have a vendor Pthread library installed.${TN}"
        echo "${TB}         There might be conflicts with its pthread.h under compile-time!${TN}"
    fi
else
    PTHREAD_O=""     dnl # only for stripped down Pth source tree
    LIBPTHREAD_A=""  dnl # only for stripped down Pth source tree
    LIBPTHREAD_LA=""
    PTHREAD_CONFIG_1=""
    PTHREAD_3=""
    INSTALL_PTHREAD=""
    UNINSTALL_PTHREAD=""
    TEST_PTHREAD=""
fi
AC_SUBST(PTHREAD_O)
AC_SUBST(LIBPTHREAD_A)
AC_SUBST(LIBPTHREAD_LA)
AC_SUBST(PTHREAD_CONFIG_1)
AC_SUBST(PTHREAD_3)
AC_SUBST(INSTALL_PTHREAD)
AC_SUBST(UNINSTALL_PTHREAD)
AC_SUBST(TEST_PTHREAD)

dnl #   whether to build against OSSP ex library
AC_CHECK_EXTLIB(OSSP ex, ex, __ex_ctx, ex.h,
                AC_DEFINE(PTH_EX, 1, [define if using OSSP ex in GNU pth]))

dnl #   whether to build against Sfio library
PTH_EXT_SFIO=0
AC_CHECK_EXTLIB(Sfio, sfio, sfdisc, sfio.h, PTH_EXT_SFIO=1)
AC_SUBST(PTH_EXT_SFIO)

dnl #   whether to build against Dmalloc library
AC_CHECK_EXTLIB(Dmalloc, dmalloc, dmalloc_debug, dmalloc.h,
                AC_DEFINE(PTH_DMALLOC, 1, [define if using Dmalloc in GNU pth]))

dnl ##
dnl ##  OUTPUT SUBSTITUTION
dnl ##

AC_MSG_PART(Output Substitution)

AC_OUTPUT(dnl
Makefile dnl
pthread-config dnl
pthread.h dnl
pth-config dnl
pth.h dnl
pth_acmac.h dnl
,dnl
chmod +rx pthread-config
chmod +rx pth-config
)dnl
if test ".$enable_pthread" = .no; then
    rm -f pthread-config
    rm -f pthread.h
fi

if test ".$enable_batch" != .yes; then
    echo ""
    echo "Now please type \`${TB}make${TN}' to compile. Good luck."
    echo ""
fi

