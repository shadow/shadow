#!/usr/bin/env python2.7

# The Shadow Simulator
#
# Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
#
# This file is part of Shadow.
#
# Shadow is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Shadow is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
#

import sys, os, argparse, subprocess, multiprocessing, shlex, shutil, urllib2, tarfile, gzip, stat, time
from datetime import datetime

BUILD_PREFIX="./build"
INSTALL_PREFIX=os.path.expanduser("~/.shadow")

TOR_DEFAULT_VERSION="0.2.3.25"
TOR_URL="https://archive.torproject.org/tor-package-archive/tor-{0}.tar.gz".format(TOR_DEFAULT_VERSION)
TOR_URL_SIG="https://archive.torproject.org/tor-package-archive/tor-{0}.tar.gz.asc".format(TOR_DEFAULT_VERSION)

OPENSSL_DEFAULT_VERSION="openssl-1.0.1c"
OPENSSL_URL="https://www.openssl.org/source/{0}.tar.gz".format(OPENSSL_DEFAULT_VERSION)
OPENSSL_URL_SIG="https://www.openssl.org/source/{0}.tar.gz.asc".format(OPENSSL_DEFAULT_VERSION)

LIBEVENT_DEFAULT_VERSION="libevent-2.0.21-stable"
LIBEVENT_URL="https://github.com/downloads/libevent/libevent/{0}.tar.gz".format(LIBEVENT_DEFAULT_VERSION)
LIBEVENT_URL_SIG="https://github.com/downloads/libevent/libevent/{0}.tar.gz.asc".format(LIBEVENT_DEFAULT_VERSION)

def main():
    parser_main = argparse.ArgumentParser(
        description='Utility to help setup the Shadow simulator', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(
        help='run a subcommand (for help use <subcommand> --help)')
        
    # configure dependencies subcommand
    parser_dep = subparsers_main.add_parser('dependencies',
        help="configure and build Shadow's custom dependencies", 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_dep.set_defaults(func=dependencies, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # add dependencies options
    parser_dep.add_argument('-p', '--prefix', 
        help="configure PATH as dependency root installation directory", 
        metavar="PATH",
        action="store", dest="prefix",
        default=INSTALL_PREFIX)
        
    # configure build subcommand
    parser_build = subparsers_main.add_parser('build',
        help='configure and build Shadow', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_build.set_defaults(func=build, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # add building options
    parser_build.add_argument('-p', '--prefix', 
        help="configure PATH as Shadow root installation directory", 
        metavar="PATH",
        action="store", dest="prefix",
        default=INSTALL_PREFIX)
    
    parser_build.add_argument('-i', '--include', 
        help="append PATH to the list of paths searched for headers. useful if dependencies are installed to non-standard locations.",
        metavar="PATH",
        action="append", dest="extra_includes",
        default=[INSTALL_PREFIX+ "/include", os.path.expanduser("~/.local/include")])
    
    parser_build.add_argument('-l', '--library', 
        help="append PATH to the list of paths searched for libraries. useful if dependencies are installed to non-standard locations.",
        metavar="PATH",
        action="append", dest="extra_libraries",
        default=[INSTALL_PREFIX+ "/lib", os.path.expanduser("~/.local/lib")])
    
    parser_build.add_argument('-g', '--debug',
        help="build in extra memory checks and debugging symbols when running Shadow",
        action="store_true", dest="do_debug",
        default=False)

    parser_build.add_argument('-v', '--verbose',
        help="print verbose output from the compiler",
        action="store_true", dest="do_verbose",
        default=False)
        
    parser_build.add_argument('-j', '--jobs',
        help="number of jobs to run simultaneously during the build",
        metavar="N", type=int,
        action="store", dest="njobs",
        default=multiprocessing.cpu_count())
        
    parser_build.add_argument('-o', '--profile',
        help="build in gprof profiling information when running Shadow",
        action="store_true", dest="do_profile",
        default=False)
    
    parser_build.add_argument('-t', '--test',
        help="build tests", 
        action="store_true", dest="do_test",
        default=False)
    
    parser_build.add_argument('--tor-prefix', 
        help="PATH to a custom base Tor directory to build (overrides '--tor-version)", 
        metavar="PATH", 
        action="store", dest="tor_prefix",
        default=None)
    
    parser_build.add_argument('--tor-version', 
        help="specify which VERSION of Tor to download and build (overridden by '--tor-prefix')", 
        metavar="VERSION", 
        action="store", dest="tor_version",
        default=TOR_DEFAULT_VERSION)
    
    parser_build.add_argument('--libevent-prefix', 
        help="use non-standard PATH when linking Tor to libevent.",
        metavar="PATH",
        action="store", dest="libevent_prefix",
        default=INSTALL_PREFIX)
    
    parser_build.add_argument('--static-libevent', 
        help="tell Tor to link against the static version of libevent", 
        action="store_true", dest="static_libevent",
        default=True)
    
    parser_build.add_argument('--openssl-prefix', 
        help="use non-standard PATH when linking Tor to openssl.",
        metavar="PATH",
        action="store", dest="openssl_prefix",
        default=INSTALL_PREFIX)

    parser_build.add_argument('--static-openssl', 
        help="tell Tor to link against the static version of openssl", 
        action="store_true", dest="static_openssl",
        default=True)
    
    parser_build.add_argument('--export-libraries', 
        help="export Shadow's plug-in service libraries and headers", 
        action="store_true", dest="export_libraries",
        default=False)
        
    parser_build.add_argument('--disable-plugin-browser',
        help="do not build the built-in browser plug-in (HTML browser)", 
        action="store_true", dest="disable_browser",
        default=False)
    
    parser_build.add_argument('--disable-plugin-echo', 
        help="do not build the built-in echo plug-in (ping pong)", 
        action="store_true", dest="disable_echo",
        default=False)
    
    parser_build.add_argument('--disable-plugin-filetransfer',
        help="do not build the built-in filetransfer plug-in (HTTP client and server)", 
        action="store_true", dest="disable_filetransfer",
        default=False)
    
    parser_build.add_argument('--disable-plugin-scallion', 
        help="do not build the built-in scallion plug-in (Tor)", 
        action="store_true", dest="disable_scallion",
        default=False)
    
    parser_build.add_argument('--disable-plugin-torrent', 
        help="do not build the built-in torrent plug-in (P2P file transfers)", 
        action="store_true", dest="disable_torrent",
        default=False)
    
    parser_build.add_argument('--disable-plugin-ping', 
        help="do not build the built-in ping plug-in (pings over Tor circuits)", 
        action="store_true", dest="disable_ping",
        default=False)

    parser_build.add_argument('--enable-memory-tracker', 
        help="preload malloc and free and track nodes memory usage (experimental!)", 
        action="store_true", dest="enable_memtracker",
        default=False)
        
    parser_build.add_argument('--enable-evp-cipher', 
        help="preload EVP_Cipher to save CPU cycles on evp crypto ciphers (experimental!)", 
        action="store_true", dest="enable_evpcipher",
        default=False)
    
    # configure install subcommand
    parser_install = subparsers_main.add_parser('install', help='install Shadow', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_install.set_defaults(func=install, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    # run chosen command
    r = args.func(args)
    
    log("returning code '{0}'".format(r))

def dependencies(args):
    if args.prefix is not None: args.prefix = getfullpath(args.prefix)
    filepath=getfullpath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=getfullpath(BUILD_PREFIX)
    
    keyring=getfullpath(rootdir + "/contrib/deps_keyring.gpg")
    
    ## we will start in build directory
    if not os.path.exists(builddir): os.makedirs(builddir)
    os.chdir(builddir)
    
    openssl_tarball = get("./", OPENSSL_URL, OPENSSL_URL_SIG, keyring)
    if openssl_tarball is None: return -1
    libevent_tarball = get("./", LIBEVENT_URL, LIBEVENT_URL_SIG, keyring)
    if libevent_tarball is None: return -1
    
    ## build openssl first
    
    dir = extract(openssl_tarball)
    if dir is None: return -1
    os.chdir(dir)
    
    ## for debugging and profiling (you may want to enable -g and -pg independently)
    #configure = "./config --prefix={0} no-shared threads -fPIC -g -pg -DPURIFY -Bsymbolic".format(args.prefix)
    configure = "./config --prefix={0} shared threads -fPIC".format(args.prefix)

    log("now attempting to build openssl with '{0}'".format(configure))
    if automake(configure, "make", "make install") != 0: 
        log("ERROR!: problems building {0}".format(openssl_tarball))
        return -1
        
    ## now build libevent
        
    os.chdir(builddir)
    dir = extract(libevent_tarball)
    if dir is None: return -1
    os.chdir(dir)

    #configure = "./configure --prefix={0} --enable-shared=no CFLAGS=\"-fPIC -I{0} -g -pg\" LDFLAGS=\"-L{0}\" CPPFLAGS=\"-DUSE_DEBUG\"".format(args.prefix)
    configure = "./configure --prefix={0} --enable-shared CFLAGS=\"-fPIC -I{0}\" LDFLAGS=\"-L{0}\"".format(args.prefix)

    log("now attempting to build libevent with '{0}'".format(configure))
    if automake(configure, "make", "make install") != 0: 
        log("ERROR!: problems building {0}".format(openssl_tarball))
        return -1
    
    log("successfully installed dependencies to '{0}'".format(args.prefix))    
    return 0

def automake(configure, make, install):
    retcode = subprocess.call(shlex.split(configure))
    if retcode != 0: return retcode
    retcode = subprocess.call(shlex.split(make))
    if retcode != 0: return retcode
    retcode = subprocess.call(shlex.split(install))
    if retcode != 0: return retcode
    return 0
    
def build(args):
    # get absolute paths
    if args.prefix is not None: args.prefix = getfullpath(args.prefix)
    if args.tor_prefix is not None: args.tor_prefix = getfullpath(args.tor_prefix)
    if args.libevent_prefix is not None: args.libevent_prefix = getfullpath(args.libevent_prefix)
    if args.openssl_prefix is not None: args.openssl_prefix = getfullpath(args.openssl_prefix)
    
    args.tor_url = TOR_URL.replace(TOR_DEFAULT_VERSION, args.tor_version)
    args.tor_sig_url = TOR_URL_SIG.replace(TOR_DEFAULT_VERSION, args.tor_version)
    
    filepath=getfullpath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=getfullpath(BUILD_PREFIX)
    installdir=getfullpath(args.prefix)
    
    args.keyring = getfullpath(rootdir + "/contrib/deps_keyring.gpg")
    
    # clear cmake cache
    if os.path.exists(builddir+"/shadow"): shutil.rmtree(builddir+"/shadow")

    # create directories
    if not os.path.exists(builddir+"/shadow"): os.makedirs(builddir+"/shadow")
    if not os.path.exists(installdir): os.makedirs(installdir)
        
    # build up args string for the cmake command
    cmake_cmd = "cmake " + rootdir + " -DCMAKE_INSTALL_PREFIX=" + installdir

    # other cmake options
    if args.do_debug: cmake_cmd += " -DSHADOW_DEBUG=ON"
    if args.do_verbose: os.putenv("VERBOSE", "1")
    if args.do_test: cmake_cmd += " -DSHADOW_TEST=ON"
    if args.do_profile: cmake_cmd += " -DSHADOW_PROFILE=ON"
    if args.export_libraries: cmake_cmd += " -DSHADOW_EXPORT=ON"
    if args.enable_memtracker: cmake_cmd += " -DSHADOW_ENABLE_MEMTRACKER=ON"
    if args.enable_evpcipher: cmake_cmd += " -DSHADOW_ENABLE_EVPCIPHER=ON"
    if args.disable_browser: cmake_cmd += " -DBUILD_BROWSER=OFF"
    if args.disable_echo: cmake_cmd += " -DBUILD_ECHO=OFF"
    if args.disable_filetransfer: cmake_cmd += " -DBUILD_FILETRANSFER=OFF"
    if args.disable_scallion: cmake_cmd += " -DBUILD_SCALLION=OFF"
    if args.disable_torrent: cmake_cmd += " -DBUILD_TORRENT=OFF"
    if args.disable_ping: cmake_cmd += " -DBUILD_PING=OFF"

    # we will run from build directory
    calledDirectory = os.getcwd()
    
    # run build tasks
    os.chdir(builddir)
    
    # check if we need to setup Tor
    if not args.disable_scallion:
        args.tordir = getfullpath(builddir+"/tor")

        if setup_tor(args) != 0: return -1
        cmake_cmd += " -DSCALLION_TORPATH=" + args.tordir
        
        torversion = get_tor_version(args)
        if torversion == None: return -1
        log("detected tor version {0}".format(torversion))
        vparts = torversion.split(".")
        a, b, c, d = int(vparts[0]), int(vparts[1]), int(vparts[2]), int(vparts[3].split("-")[0])
        if c < 3 or (c == 3 and d < 5): 
            cmake_cmd += " -DSCALLION_SKIPREFILL=1"
            log("Tor configured to use refill callbacks")
    
    # now we will be using cmake to build shadow and the plug-ins
    os.chdir(builddir+"/shadow")
    
    # hack to make passing args to CMAKE work... doesnt seem to like the first arg
    args.extra_includes.insert(0, "./")
    args.extra_libraries.insert(0, "./")
    
    # add extra library and include directories as absolution paths
    make_paths_absolute(args.extra_includes)
    make_paths_absolute(args.extra_libraries)

    # make sure we can access them from cmake
    cmake_cmd += " -DCMAKE_EXTRA_INCLUDES=" + ';'.join(args.extra_includes)
    cmake_cmd += " -DCMAKE_EXTRA_LIBRARIES=" + ';'.join(args.extra_libraries)

    # look for the clang/clang++ compilers
    clangccpath = which("clang")
    if clangccpath is None: 
        log("ERROR: can't find 'clang' compiler in your PATH! Is it installed? (Do you want the 'dependencies' subcommand?)")
    clangcxxpath = which("clang++")
    if clangcxxpath is None: 
        log("ERROR: can't find 'clang++' compiler in your PATH! Is it installed? (Do you want the 'dependencies' subcommand?)")
    if clangccpath is None or clangcxxpath is None: return -1
    
    # set clang/llvm as compiler
    os.putenv("CC", clangccpath)
    os.putenv("CXX", clangcxxpath)
    #cmake_cmd += " -D_CMAKE_TOOLCHAIN_PREFIX=llvm-"
    
    # call cmake to configure the make process, wait for completion
    log("running \'{0}\' from \'{1}\'".format(cmake_cmd, os.getcwd()))
    retcode = subprocess.call(cmake_cmd.strip().split())
    log("cmake returned " + str(retcode))
    
    if retcode == 0:
        # call make, wait for it to finish
        make = "make -j{0}".format(args.njobs)
        log("calling " + make)
        retcode = subprocess.call(shlex.split(make))
        log("make returned " + str(retcode))
        if retcode == 0: log("now run \'python setup.py install\'")
        else: log("ERROR! Non-zero return code from make.")
    else: log("ERROR! Non-zero return code from cmake.")

    # go back to where we came from
    os.chdir(calledDirectory)
    return retcode

def install(args):
    builddir=getfullpath(BUILD_PREFIX+"/shadow")
    if not os.path.exists(builddir): 
        log("ERROR: please build before installing!")
        return

    # go to build dir and install from makefile
    calledDirectory = os.getcwd()
    os.chdir(builddir)
    
    # call make install, wait for it to finish
    makeCommand = "make install"
    
    log("calling \'"+makeCommand+"\'")
    retcode = subprocess.call(makeCommand.strip().split())
    log("make install returned " + str(retcode))
    if retcode == 0: log("now run \'shadow\' from \'PREFIX/bin\' (check your PATH)")
    
    # go back to where we came from
    os.chdir(calledDirectory)
    return retcode

def setup_tor(args):
    log("checking Tor source dependencies...")

    if which("aclocal") is None or which("autoheader") is None or which("autoconf") is None or which("automake") is None:
        log("ERROR!: missing dependencies - please install 'automake' and 'autoconf', or make sure they are in your PATH")
        return -1
    
    # if custom Tor prefix is given, always blow away Tor's build directory
    # and clean the potentially dirty custom directory
    if args.tor_prefix is not None:
        if os.path.exists(args.tordir): shutil.rmtree(args.tordir)
        shutil.copytree(args.tor_prefix, args.tordir)
        distcleancmd = "make distclean"
        subprocess.call(shlex.split(distcleancmd.strip()))
    
    if not os.path.exists(args.tordir):
        # we have no Tor build directory, check requested url
        target_tor = getfullpath(os.path.basename(args.tor_url))
    
        # only download if we dont have the requested URL
        if not os.path.exists(target_tor):
            if get("./", args.tor_url, args.tor_sig_url, args.keyring) == None:
#            if download(args.tor_url, target_tor) != 0: 
                log("ERROR!: problem downloading \'{0}\'".format(args.tor_url))
                return -1

        # we are either extracting a cached tarball, or one we just downloaded
        d = extract(target_tor)
        if d is not None:
            shutil.copytree(d, args.tordir)
            shutil.rmtree(d)
        else: 
            log("ERROR!: \'{0}\' is not a tarfile".format(target_tor))
            return -1
    
    retcode = 0

    # Copy patch and static symbol converter scripts to the build directory
    shutil.copy("../contrib/patch.sh", args.tordir)

    # if we already configured successfully, dont patch or re-configure
    if os.path.exists(args.tordir):
        os.chdir(args.tordir)
        if not os.path.exists(args.tordir+"/orconfig.h"):
            cflags = "-fPIC -fno-inline"
            if args.extra_includes is not None:
                for i in args.extra_includes: cflags += " -I" + i.strip()
            if args.do_debug: cflags += " -g"
            
            ldflags = ""
            if args.extra_libraries is not None:
                for l in args.extra_libraries: ldflags += " -L" + l.strip()

            gen = "aclocal && autoheader && autoconf && automake --add-missing --copy"
            configure = "./configure --disable-transparent --disable-asciidoc --disable-threads CFLAGS=\"" + cflags + "\" LDFLAGS=\"" + ldflags + "\" LIBS=-lrt"

            if args.libevent_prefix is not None: configure += " --with-libevent-dir=" + getfullpath(args.libevent_prefix)
            if args.openssl_prefix is not None: configure += " --with-openssl-dir=" + getfullpath(args.openssl_prefix)
            if args.static_openssl: configure += " --enable-static-openssl"
            if args.static_libevent: configure += " --enable-static-libevent"

            # generate configure
            log("running \'{0}\'".format(gen))
            for cmd in gen.split('&&'):
                retcode = retcode | subprocess.call(shlex.split(cmd.strip()))
            if retcode !=0: return retcode
            
            # need to patch AFTER generating configure to avoid overwriting the patched configure
            # patch static variables/functions
            patch = "./patch.sh"
            log("running \'{0}\'".format(patch))
            retcode = subprocess.call(shlex.split(patch))
            if retcode !=0: return retcode
            
            # configure
            log("running \'{0}\'".format(configure))
            retcode = subprocess.call(shlex.split(configure))
            if retcode !=0: return retcode

            make = "make -j{0}".format(args.njobs)
            log("running \'{0}\'".format(make))
            retcode = subprocess.call(shlex.split(make))
        
    return retcode

def get_tor_version(args):
    # get tor version info
    orconf = getfullpath(args.tordir+"/orconfig.h")
    if not os.path.exists(orconf): 
        log("ERROR: file \'{0}\' does not exist".format(orconf))
        return None
    
    search = "#define VERSION "
    
    with open(orconf, 'rb') as f:
        for line in f:
            if line.find(search) > -1:
                return line[line.index("\"")+1:line.rindex("\"")]
               
    log("ERROR: cant find version information in config file \'{0}\'".format(orconf)) 
    return None

def getfullpath(path):
    return os.path.abspath(os.path.expanduser(path))

def make_paths_absolute(list):
    for i in xrange(len(list)): list[i] = getfullpath(list[i])

def extract(tarball):    
    if tarfile.is_tarfile(tarball):
        tar = tarfile.open(tarball, "r:gz")
        n = tar.next().name
        while n.find('/') < 0: n = tar.next().name
        d = getfullpath(os.getcwd() + "/" + n[:n.index('/')])
        #tar.extractall()
        subprocess.call(shlex.split("tar xaf {0}".format(tarball)))
        tar.close()
        return d
    else: 
        log("ERROR!: \'{0}\' is not a tarfile".format(tarball))
        return None

def get(targetdir, fileurl, sigurl, keyring):
    targetfile = getfullpath(targetdir + "/" + os.path.basename(fileurl))
    targetsig = getfullpath(targetdir + "/" + os.path.basename(sigurl))
    
    if not os.path.exists(targetfile) and (download(fileurl, targetfile) < 0): return None

    if not query_yes_no("Do you want to check the signature of {0}?".format(os.path.basename(fileurl))): return targetfile

    if (not os.path.exists(targetsig)) and (download(sigurl, targetsig) < 0): return None
    
    question = "Do you want to use the included keyring instead of the running user's keyring?"
    retcode = 0
    if query_yes_no(question):
        gpg = "gpg --keyring {0} --verify {1}".format(keyring, targetsig)
        log("running \'{0}\'".format(gpg))
        retcode = subprocess.call(shlex.split(gpg))
    else:
        gpg = "gpg --verify {0}".format(targetsig)
        log("running \'{0}\'".format(gpg))
        retcode = subprocess.call(shlex.split(gpg))

    if retcode == 0: 
        log("Signature is good")
        time.sleep(1)
        return targetfile
    else: 
        log("WARNING!: signature is bad")
        time.sleep(1)
        if query_yes_no("Do you want to continue without verifying the signature?"): return targetfile
        else: return None
        
def download(url, target_path):
    if query_yes_no("May we download \'{0}\'?".format(url)):
        log("attempting to download " + url)
        try:
            u = urllib2.urlopen(url)
            localfile = open(target_path, 'w')
            localfile.write(u.read())
            localfile.close()
            log("successfully downloaded \'{0}\'".format(url))
            return 0
        except urllib2.URLError:
            log("ERROR!: failed to download \'{0}\'".format(url))
            return -1
    else:
        log("ERROR!: user denied download permission.")
        return -1
    
def query_yes_no(question, default="yes"):
    """Ask a yes/no question via raw_input() and return their answer.

    "question" is a string that is presented to the user.
    "default" is the presumed answer if the user just hits <Enter>.
        It must be "yes" (the default), "no" or None (meaning
        an answer is required of the user).

    The "answer" return value is one of "yes" or "no".
    """
    valid = {"yes":True,   "y":True,  "ye":True, "no":False,     "n":False}
    if default == None: prompt = " [y/n] "
    elif default == "yes": prompt = " [Y/n] "
    elif default == "no": prompt = " [y/N] "
    else: raise ValueError("invalid default answer: '%s'" % default)
    
    color_start_code = "\033[1m" + "\033[93m" # <- bold+yellow
    color_end_code = "\033[0m"
    while True:
        sys.stdout.write(color_start_code + question + color_end_code + prompt)
        choice = raw_input().lower()
        if default is not None and choice == '': return valid[default]
        elif choice in valid: return valid[choice]
        else: sys.stdout.write("Please respond with 'yes' or 'no' (or 'y' or 'n').\n")

## helper - test if program is in path
def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None
    
def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] Shadow Setup: "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
