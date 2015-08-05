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
dnl ##  pth.m4: Autoconf macro for locating GNU Pth from within
dnl ##          configure.ac of third-party software packages
dnl ##

dnl ##
dnl ##  Synopsis:
dnl ##  AC_CHECK_PTH([MIN-VERSION [,          # minimum Pth version, e.g. 1.2.0
dnl ##                DEFAULT-WITH-PTH [,     # default value for --with-pth option
dnl ##                DEFAULT-WITH-PTH-TEST [,# default value for --with-pth-test option
dnl ##                EXTEND-VARS [,          # whether CFLAGS/LDFLAGS/etc are extended
dnl ##                ACTION-IF-FOUND [,      # action to perform if Pth was found
dnl ##                ACTION-IF-NOT-FOUND     # action to perform if Pth was not found
dnl ##                ]]]]]])
dnl ##  Examples:
dnl ##  AC_CHECK_PTH(1.2.0)
dnl ##  AC_CHECK_PTH(1.2.0,,,no,CFLAGS="$CFLAGS -DHAVE_PTH $PTH_CFLAGS")
dnl ##  AC_CHECK_PTH(1.2.0,yes,yes,yes,CFLAGS="$CFLAGS -DHAVE_PTH")
dnl ##
dnl
dnl #   auxilliary macros
AC_DEFUN([_AC_PTH_ERROR], [dnl
AC_MSG_RESULT([*FAILED*])
m4_define(_ac_pth_line,dnl
"+------------------------------------------------------------------------+")
echo " _ac_pth_line" 1>&2
cat <<EOT | sed -e 's/^[[ 	]]*/ | /' -e 's/>>/  /' 1>&2
$1
EOT
echo " _ac_pth_line" 1>&2
exit 1
])
AC_DEFUN([_AC_PTH_VERBOSE], [dnl
if test ".$verbose" = .yes; then
    AC_MSG_RESULT([  $1])
fi
])
dnl #   the user macro
AC_DEFUN([AC_CHECK_PTH], [dnl
dnl
dnl #   prerequisites
AC_REQUIRE([AC_PROG_CC])dnl
AC_REQUIRE([AC_PROG_CPP])dnl
dnl
PTH_CPPFLAGS=''
PTH_CFLAGS=''
PTH_LDFLAGS=''
PTH_LIBS=''
AC_SUBST(PTH_CPPFLAGS)
AC_SUBST(PTH_CFLAGS)
AC_SUBST(PTH_LDFLAGS)
AC_SUBST(PTH_LIBS)
dnl #   command line options
AC_MSG_CHECKING(for GNU Pth)
_AC_PTH_VERBOSE([])
AC_ARG_WITH(pth,dnl
[  --with-pth[=ARG]        Build with GNU Pth Library  (default=]ifelse([$2],,yes,$2)[)],dnl
,dnl
with_pth="ifelse([$2],,yes,$2)"
)dnl
AC_ARG_WITH(pth-test,dnl
[  --with-pth-test         Perform GNU Pth Sanity Test (default=]ifelse([$3],,yes,$3)[)],dnl
,dnl
with_pth_test="ifelse([$3],,yes,$3)"
)dnl
_AC_PTH_VERBOSE([+ Command Line Options:])
_AC_PTH_VERBOSE([    o --with-pth=$with_pth])
_AC_PTH_VERBOSE([    o --with-pth-test=$with_pth_test])
dnl
dnl #   configuration
if test ".$with_pth" != .no; then
    _pth_subdir=no
    _pth_subdir_opts=''
    case "$with_pth" in
        subdir:* )
            _pth_subdir=yes
            changequote(, )dnl
            _pth_subdir_opts=`echo $with_pth | sed -e 's/^subdir:[^ 	]*[ 	]*//'`
            with_pth=`echo $with_pth | sed -e 's/^subdir:\([^ 	]*\).*$/\1/'`
            changequote([, ])dnl
            ;;
    esac
    _pth_version=""
    _pth_location=""
    _pth_type=""
    _pth_cppflags=""
    _pth_cflags=""
    _pth_ldflags=""
    _pth_libs=""
    if test ".$with_pth" = .yes; then
        #   via config script in $PATH
        changequote(, )dnl
        _pth_version=`(pth-config --version) 2>/dev/null |\
                      sed -e 's/^.*\([0-9]\.[0-9]*[ab.][0-9]*\).*$/\1/'`
        changequote([, ])dnl
        if test ".$_pth_version" != .; then
            _pth_location=`pth-config --prefix`
            _pth_type='installed'
            _pth_cppflags=`pth-config --cflags`
            _pth_cflags=`pth-config --cflags`
            _pth_ldflags=`pth-config --ldflags`
            _pth_libs=`pth-config --libs --all`
        fi
    elif test -d "$with_pth"; then
        with_pth=`echo $with_pth | sed -e 's;/*$;;'`
        _pth_found=no
        #   via locally included source tree
        if test ".$_pth_subdir" = .yes; then
            _pth_location="$with_pth"
            _pth_type='local'
            _pth_cppflags="-I$with_pth"
            _pth_cflags="-I$with_pth"
            if test -f "$with_pth/ltconfig"; then
                _pth_ldflags="-L$with_pth/.libs"
            else
                _pth_ldflags="-L$with_pth"
            fi
            _pth_libs="-lpth"
            changequote(, )dnl
            _pth_version=`grep '^const char PTH_Hello' $with_pth/pth_vers.c |\
                          sed -e 's;^.*Version[ 	]*\([0-9]*\.[0-9]*[.ab][0-9]*\)[ 	].*$;\1;'`
            changequote([, ])dnl
            _pth_found=yes
            ac_configure_args="$ac_configure_args --enable-subdir $_pth_subdir_opts"
            with_pth_test=no
        fi
        #   via config script under a specified directory
        #   (a standard installation, but not a source tree)
        if test ".$_pth_found" = .no; then
            for _dir in $with_pth/bin $with_pth; do
                if test -f "$_dir/pth-config"; then
                    test -f "$_dir/pth-config.in" && continue # pth-config in source tree!
                    changequote(, )dnl
                    _pth_version=`($_dir/pth-config --version) 2>/dev/null |\
                                  sed -e 's/^.*\([0-9]\.[0-9]*[ab.][0-9]*\).*$/\1/'`
                    changequote([, ])dnl
                    if test ".$_pth_version" != .; then
                        _pth_location=`$_dir/pth-config --prefix`
                        _pth_type="installed"
                        _pth_cppflags=`$_dir/pth-config --cflags`
                        _pth_cflags=`$_dir/pth-config --cflags`
                        _pth_ldflags=`$_dir/pth-config --ldflags`
                        _pth_libs=`$_dir/pth-config --libs --all`
                        _pth_found=yes
                        break
                    fi
                fi
            done
        fi
        #   in any subarea under a specified directory
        #   (either a special installation or a Pth source tree)
        if test ".$_pth_found" = .no; then
            changequote(, )dnl
            _pth_found=0
            for _file in x `find $with_pth -name "pth.h" -type f -print`; do
                test .$_file = .x && continue
                _dir=`echo $_file | sed -e 's;[^/]*$;;' -e 's;\(.\)/$;\1;'`
                _pth_version=`($_dir/pth-config --version) 2>/dev/null |\
                              sed -e 's/^.*\([0-9]\.[0-9]*[ab.][0-9]*\).*$/\1/'`
                if test ".$_pth_version" = .; then
                    _pth_version=`grep '^#define PTH_VERSION_STR' $_file |\
                                  sed -e 's;^#define[ 	]*PTH_VERSION_STR[ 	]*"\([0-9]*\.[0-9]*[.ab][0-9]*\)[ 	].*$;\1;'`
                fi
                _pth_cppflags="-I$_dir"
                _pth_cflags="-I$_dir"
                _pth_found=`expr $_pth_found + 1`
            done
            for _file in x `find $with_pth -name "libpth.[aso]" -type f -print`; do
                test .$_file = .x && continue
                _dir=`echo $_file | sed -e 's;[^/]*$;;' -e 's;\(.\)/$;\1;'`
                _pth_ldflags="-L$_dir"
                _pth_libs="-lpth"
                _pth_found=`expr $_pth_found + 1`
            done
            changequote([, ])dnl
            if test ".$_pth_found" = .2; then
                _pth_location="$with_pth"
                _pth_type="uninstalled"
            else
                _pth_version=''
            fi
        fi
    fi
    _AC_PTH_VERBOSE([+ Determined Location:])
    _AC_PTH_VERBOSE([    o path: $_pth_location])
    _AC_PTH_VERBOSE([    o type: $_pth_type])
    if test ".$_pth_version" = .; then
        if test ".$with_pth" != .yes; then
             _AC_PTH_ERROR([dnl
             Unable to locate GNU Pth under $with_pth.
             Please specify the correct path to either a GNU Pth installation tree
             (use --with-pth=DIR if you used --prefix=DIR for installing GNU Pth in
             the past) or to a GNU Pth source tree (use --with-pth=DIR if DIR is a
             path to a pth-X.Y.Z/ directory; but make sure the package is already
             built, i.e., the "configure; make" step was already performed there).])
        else
             _AC_PTH_ERROR([dnl
             Unable to locate GNU Pth in any system-wide location (see \$PATH).
             Please specify the correct path to either a GNU Pth installation tree
             (use --with-pth=DIR if you used --prefix=DIR for installing GNU Pth in
             the past) or to a GNU Pth source tree (use --with-pth=DIR if DIR is a
             path to a pth-X.Y.Z/ directory; but make sure the package is already
             built, i.e., the "configure; make" step was already performed there).])
        fi
    fi
    dnl #
    dnl #  Check whether the found version is sufficiently new
    dnl #
    _req_version="ifelse([$1],,1.0.0,$1)"
    for _var in _pth_version _req_version; do
        eval "_val=\"\$${_var}\""
        _major=`echo $_val | sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\([[ab.]]\)\([[0-9]]*\)/\1/'`
        _minor=`echo $_val | sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\([[ab.]]\)\([[0-9]]*\)/\2/'`
        _rtype=`echo $_val | sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\([[ab.]]\)\([[0-9]]*\)/\3/'`
        _micro=`echo $_val | sed 's/\([[0-9]]*\)\.\([[0-9]]*\)\([[ab.]]\)\([[0-9]]*\)/\4/'`
        case $_rtype in
            "a" ) _rtype=0 ;;
            "b" ) _rtype=1 ;;
            "." ) _rtype=2 ;;
        esac
        _hex=`echo dummy | awk '{ printf("%d%02d%1d%02d", major, minor, rtype, micro); }' \
              "major=$_major" "minor=$_minor" "rtype=$_rtype" "micro=$_micro"`
        eval "${_var}_hex=\"\$_hex\""
    done
    _AC_PTH_VERBOSE([+ Determined Versions:])
    _AC_PTH_VERBOSE([    o existing: $_pth_version -> 0x$_pth_version_hex])
    _AC_PTH_VERBOSE([    o required: $_req_version -> 0x$_req_version_hex])
    _ok=0
    if test ".$_pth_version_hex" != .; then
        if test ".$_req_version_hex" != .; then
            if test $_pth_version_hex -ge $_req_version_hex; then
                _ok=1
            fi
        fi
    fi
    if test ".$_ok" = .0; then
        _AC_PTH_ERROR([dnl
        Found Pth version $_pth_version, but required at least version $_req_version.
        Upgrade Pth under $_pth_location to $_req_version or higher first, please.])
    fi
    dnl #
    dnl #   Perform Pth Sanity Compile Check
    dnl #
    if test ".$with_pth_test" = .yes; then
        _ac_save_CPPFLAGS="$CPPFLAGS"
        _ac_save_CFLAGS="$CFLAGS"
        _ac_save_LDFLAGS="$LDFLAGS"
        _ac_save_LIBS="$LIBS"
        CPPFLAGS="$CPPFLAGS $_pth_cppflags"
        CFLAGS="$CFLAGS $_pth_cflags"
        LDFLAGS="$LDFLAGS $_pth_ldflags"
        LIBS="$LIBS $_pth_libs"
        _AC_PTH_VERBOSE([+ Test Build Environment:])
        _AC_PTH_VERBOSE([    o CPPFLAGS=\"$CPPFLAGS\"])
        _AC_PTH_VERBOSE([    o CFLAGS=\"$CFLAGS\"])
        _AC_PTH_VERBOSE([    o LDFLAGS=\"$LDFLAGS\"])
        _AC_PTH_VERBOSE([    o LIBS=\"$LIBS\"])
        cross_compile=no
        m4_define(_code1, [dnl
        #include <stdio.h>
        #include <pth.h>
        ])
        m4_define(_code2, [dnl
        int main(int argc, char *argv[])
        {
            FILE *fp;
            if (!(fp = fopen("conftestval", "w")))
                exit(1);
            fprintf(fp, "hmm");
            fclose(fp);
            pth_init();
            pth_kill();
            if (!(fp = fopen("conftestval", "w")))
                exit(1);
            fprintf(fp, "yes");
            fclose(fp);
            exit(0);
        }
        ])
        _AC_PTH_VERBOSE([+ Performing Sanity Checks:])
        _AC_PTH_VERBOSE([    o pre-processor test])
        AC_TRY_CPP(_code1, _ok=yes, _ok=no)
        if test ".$_ok" != .yes; then
            _AC_PTH_ERROR([dnl
            Found GNU Pth $_pth_version under $_pth_location, but
            was unable to perform a sanity pre-processor check. This means
            the GNU Pth header pth.h was not found.
            We used the following build environment:
            >> CPP="$CPP"
            >> CPPFLAGS="$CPPFLAGS"
            See config.log for possibly more details.])
        fi
        _AC_PTH_VERBOSE([    o link check])
        AC_TRY_LINK(_code1, _code2, _ok=yes, _ok=no)
        if test ".$_ok" != .yes; then
            _AC_PTH_ERROR([dnl
            Found GNU Pth $_pth_version under $_pth_location, but
            was unable to perform a sanity linker check. This means
            the GNU Pth library libpth.a was not found.
            We used the following build environment:
            >> CC="$CC"
            >> CFLAGS="$CFLAGS"
            >> LDFLAGS="$LDFLAGS"
            >> LIBS="$LIBS"
            See config.log for possibly more details.])
        fi
        _AC_PTH_VERBOSE([    o run-time check])
        AC_TRY_RUN(_code1 _code2, _ok=`cat conftestval`, _ok=no, _ok=no)
        if test ".$_ok" != .yes; then
            if test ".$_ok" = .no; then
                _AC_PTH_ERROR([dnl
                Found GNU Pth $_pth_version under $_pth_location, but
                was unable to perform a sanity execution check. This usually
                means that the GNU Pth shared library libpth.so is present
                but \$LD_LIBRARY_PATH is incomplete to execute a Pth test.
                In this case either disable this test via --without-pth-test,
                or extend \$LD_LIBRARY_PATH, or build GNU Pth as a static
                library only via its --disable-shared Autoconf option.
                We used the following build environment:
                >> CC="$CC"
                >> CFLAGS="$CFLAGS"
                >> LDFLAGS="$LDFLAGS"
                >> LIBS="$LIBS"
                See config.log for possibly more details.])
            else
                _AC_PTH_ERROR([dnl
                Found GNU Pth $_pth_version under $_pth_location, but
                was unable to perform a sanity run-time check. This usually
                means that the GNU Pth library failed to work and possibly
                caused a core dump in the test program. In this case it
                is strongly recommended that you re-install GNU Pth and this
                time make sure that it really passes its "make test" procedure.
                We used the following build environment:
                >> CC="$CC"
                >> CFLAGS="$CFLAGS"
                >> LDFLAGS="$LDFLAGS"
                >> LIBS="$LIBS"
                See config.log for possibly more details.])
            fi
        fi
        _extendvars="ifelse([$4],,yes,$4)"
        if test ".$_extendvars" != .yes; then
            CPPFLAGS="$_ac_save_CPPFLAGS"
            CFLAGS="$_ac_save_CFLAGS"
            LDFLAGS="$_ac_save_LDFLAGS"
            LIBS="$_ac_save_LIBS"
        fi
    else
        _extendvars="ifelse([$4],,yes,$4)"
        if test ".$_extendvars" = .yes; then
            if test ".$_pth_subdir" = .yes; then
                CPPFLAGS="$CPPFLAGS $_pth_cppflags"
                CFLAGS="$CFLAGS $_pth_cflags"
                LDFLAGS="$LDFLAGS $_pth_ldflags"
                LIBS="$LIBS $_pth_libs"
            fi
        fi
    fi
    PTH_CPPFLAGS="$_pth_cppflags"
    PTH_CFLAGS="$_pth_cflags"
    PTH_LDFLAGS="$_pth_ldflags"
    PTH_LIBS="$_pth_libs"
    AC_SUBST(PTH_CPPFLAGS)
    AC_SUBST(PTH_CFLAGS)
    AC_SUBST(PTH_LDFLAGS)
    AC_SUBST(PTH_LIBS)
    _AC_PTH_VERBOSE([+ Final Results:])
    _AC_PTH_VERBOSE([    o PTH_CPPFLAGS=\"$PTH_CPPFLAGS\"])
    _AC_PTH_VERBOSE([    o PTH_CFLAGS=\"$PTH_CFLAGS\"])
    _AC_PTH_VERBOSE([    o PTH_LDFLAGS=\"$PTH_LDFLAGS\"])
    _AC_PTH_VERBOSE([    o PTH_LIBS=\"$PTH_LIBS\"])
fi
if test ".$with_pth" != .no; then
    AC_MSG_RESULT([version $_pth_version, $_pth_type under $_pth_location])
    ifelse([$5], , :, [$5])
else
    AC_MSG_RESULT([no])
    ifelse([$6], , :, [$6])
fi
])

