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

import sys, os, argparse, subprocess, shlex, shutil, urllib2, tarfile, gzip, stat
from datetime import datetime

BUILD_PREFIX="./build"
INSTALL_PREFIX=os.path.expanduser("~/.shadow")

TOR_DEFAULT_VERSION="0.2.3.20-rc"
TOR_URL="https://archive.torproject.org/tor-package-archive/tor-" + TOR_DEFAULT_VERSION + ".tar.gz"

def main():
    parser_main = argparse.ArgumentParser(
        description='Utility to help setup the Shadow simulator', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(
        help='run a subcommand (for help use <subcommand> --help)')
    
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
    
    # configure install subcommand
    parser_install = subparsers_main.add_parser('install', help='install Shadow', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_install.set_defaults(func=install, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    # run chosen command
    args.func(args)

def build(args):
    # get absolute paths
    if args.prefix is not None: args.prefix = os.path.abspath(args.prefix)
    if args.tor_prefix is not None: args.tor_prefix = os.path.abspath(args.tor_prefix)
    if args.libevent_prefix is not None: args.libevent_prefix = os.path.abspath(args.libevent_prefix)
    if args.openssl_prefix is not None: args.openssl_prefix = os.path.abspath(args.openssl_prefix)
    
    args.tor_url = TOR_URL.replace(TOR_DEFAULT_VERSION, args.tor_version)
    
    filepath=os.path.abspath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=os.path.abspath(BUILD_PREFIX)
    installdir=os.path.abspath(args.prefix)
    
    # clear cmake cache
    if os.path.exists(builddir+"/cmake"): shutil.rmtree(builddir+"/cmake")

    # create directories
    if not os.path.exists(builddir+"/cmake"): os.makedirs(builddir+"/cmake")
    if not os.path.exists(installdir): os.makedirs(installdir)
        
    # build up args string for the cmake command
    cmake_cmd = "cmake " + rootdir + " -DCMAKE_BUILD_PREFIX=" + builddir + " -DCMAKE_INSTALL_PREFIX=" + installdir
    
    # other cmake options
    if args.do_debug: cmake_cmd += " -DSHADOW_DEBUG=ON"
    if args.do_test: cmake_cmd += " -DSHADOW_TEST=ON"
    if args.do_profile: cmake_cmd += " -DSHADOW_PROFILE=ON"
    if args.export_libraries: cmake_cmd += " -DSHADOW_EXPORT=ON"
    if args.disable_browser: cmake_cmd += " -DBUILD_BROWSER=OFF"
    if args.disable_echo: cmake_cmd += " -DBUILD_ECHO=OFF"
    if args.disable_filetransfer: cmake_cmd += " -DBUILD_FILETRANSFER=OFF"
    if args.disable_scallion: cmake_cmd += " -DBUILD_SCALLION=OFF"
    if args.disable_torrent: cmake_cmd += " -DBUILD_TORRENT=OFF"

    # we will run from build directory
    calledDirectory = os.getcwd()
    
    # run build tasks
    os.chdir(builddir)
    if generate_files() != 0: return
    
    # check if we need to setup Tor
    if not args.disable_scallion:
        args.tordir = os.path.abspath(builddir+"/tor")
        args.extra_includes.extend([args.tordir, args.tordir+"/src/or", args.tordir+"/src/common"])
        args.extra_libraries.extend([args.tordir, args.tordir+"/src/or", args.tordir+"/src/common"])

        if setup_tor(args) != 0: return -1
        cmake_cmd += " -DSCALLION_TORPATH=" + args.tordir
        
        torversion = get_tor_version(args)
        if torversion == None: return -1
        log("detected tor version {0}".format(torversion))
        vparts = torversion.split(".")
        a, b, c, d = int(vparts[0]), int(vparts[1]), int(vparts[2]), int(vparts[3].split("-")[0])
        if c >= 3 and d >= 5: 
            cmake_cmd += " -DSCALLION_DOREFILL=1"
            log("Tor configured to use refill callbacks")
    
    
    # now we will be using cmake to build shadow and the plug-ins
    os.chdir(builddir+"/cmake")
    
    # hack to make passing args to CMAKE work... doesnt seem to like the first arg
    args.extra_includes.insert(0, "./")
    args.extra_libraries.insert(0, "./")
    
    # add extra library and include directories as absolution paths
    make_paths_absolute(args.extra_includes)
    make_paths_absolute(args.extra_libraries)

    # make sure we can access them from cmake
    cmake_cmd += " -DCMAKE_EXTRA_INCLUDES=" + ';'.join(args.extra_includes)
    cmake_cmd += " -DCMAKE_EXTRA_LIBRARIES=" + ';'.join(args.extra_libraries)
    
    # call cmake to configure the make process, wait for completion
    log("running \'{0}\' from \'{1}\'".format(cmake_cmd, os.getcwd()))
    retcode = subprocess.call(cmake_cmd.strip().split())
    log("cmake returned " + str(retcode))
    
    if retcode == 0:
        # call make, wait for it to finish
        log("calling \'make\'")
        retcode = subprocess.call(["make"])
        log("make returned " + str(retcode))
        if retcode == 0: log("now run \'python setup.py install\'")
        else: log("ERROR! Non-zero return code from make.")

    # go back to where we came from
    os.chdir(calledDirectory)
    return retcode

def install(args):
    builddir=os.path.abspath(BUILD_PREFIX+"/cmake")
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
    
    # if custom Tor prefix given, always blow away Tor's build directory
    if args.tor_prefix is not None:
        if os.path.exists(args.tordir): shutil.rmtree(args.tordir)
        shutil.copytree(args.tor_prefix, args.tordir)
    
    if not os.path.exists(args.tordir):
        # we have no Tor build directory, check requested url
        target_tor = os.path.abspath(os.path.basename(args.tor_url))
    
        # only download if we dont have the requested URL
        if not os.path.exists(target_tor):
            if download(args.tor_url, target_tor) != 0: 
                log("ERROR!: problem downloading \'{0}\'".format(args.tor_url))
                return -1

        # we are either extracting a cached tarball, or one we just downloaded
        if tarfile.is_tarfile(target_tor):
            tar = tarfile.open(target_tor, "r:gz")
            n = tar.next().name
            while n.find('/') < 0: n = tar.next().name
            d = os.path.abspath(os.getcwd() + "/" + n[:n.index('/')])
            tar.extractall()
            tar.close()
            shutil.copytree(d, args.tordir)
            shutil.rmtree(d)
        else: 
            log("ERROR!: \'{0}\' is not a tarfile".format(target_tor))
            return -1
    
    log("building Tor from source...")
        
    retcode = 0

    # Copy patch and static symbol converter scripts to the build directory
    shutil.copy("../contrib/static_symbol_converter.py", args.tordir)
    shutil.copy("../contrib/patch.sh", args.tordir)

    # if we already built successfully, dont patch or re-configure
    if os.path.exists(args.tordir):
        os.chdir(args.tordir)
        if not os.path.exists(args.tordir+"/src/or/tor"):
            # patch then configure first
            cflags = "-fPIC -fno-inline"
            if args.extra_includes is not None:
                for i in args.extra_includes: cflags += " -I" + i.strip()
            if args.do_debug: cflags += " -g"
            
            ldflags = ""
            if args.extra_libraries is not None:
                for l in args.extra_libraries: ldflags += " -L" + l.strip()
        
            patch = "./patch.sh"
            gen = "aclocal && autoheader && autoconf && automake --add-missing --copy"
            configure = "./configure --disable-transparent --disable-asciidoc CFLAGS=\"" + cflags + "\" LDFLAGS=\"" + ldflags + "\" LIBS=-lrt"

            if args.libevent_prefix is not None: configure += " --with-libevent-dir=" + os.path.abspath(args.libevent_prefix)
            if args.openssl_prefix is not None: configure += " --with-openssl-dir=" + os.path.abspath(args.openssl_prefix)
            if args.static_openssl: configure += " --enable-static-openssl"
            if args.static_libevent: configure += " --enable-static-libevent"

            if retcode == 0:
                # generate configure
                log("running \'{0}\'".format(gen))
                for cmd in gen.split('&&'):
                    retcode = retcode | subprocess.call(shlex.split(cmd.strip()))
            
            # need to patch AFTER generating configure to avoid overwriting the patched configure
            if retcode == 0:
                # patch
                log("running \'{0}\'".format(patch))
                retcode = subprocess.call(shlex.split(patch))
            
            if retcode == 0:
                # configure
                log("running \'{0}\'".format(configure))
                retcode = subprocess.call(shlex.split(configure))

        # configure done now
        if retcode == 0:
            # build
            build = "make clean && make"
            log("running \'{0}\'".format(build))
            for cmd in build.split('&&'):
                retcode = retcode | subprocess.call(shlex.split(cmd.strip()))
    
        if retcode == 0:
            convert = "python static_symbol_converter.py " + args.tor_version
            log("running \'{0}\'".format(convert))
            retcode = subprocess.call(shlex.split(convert))

        if retcode == 0:
            make = "make"
            log("running \'{0}\'".format(make))
            retcode = subprocess.call(shlex.split(make))

        # if we had a failure, start over with patching
        if retcode != 0:
            #shutil.rmtree(args.tordir)
            return -1
        
    return retcode


def get_tor_version(args):
    # get tor version info
    orconf = os.path.abspath(args.tordir+"/orconfig.h")
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

def make_paths_absolute(list):
    for i in xrange(len(list)): list[i] = os.path.abspath(list[i])
    
def generate_files():
    dd("1KiB.urnd", 1)
    dd("16KiB.urnd", 16)
    dd("32KiB.urnd", 32)
    dd("50KiB.urnd", 50)
    dd("320KiB.urnd", 320)
    dd("1MiB.urnd", 1024)
    dd("5MiB.urnd", 5120)
    return 0
    
def dd(filename, kb):
    if not os.path.exists(filename):
        ddcmd = "/bin/dd if=/dev/urandom of=" + filename + " bs=1024 count=" + str(kb)
        log("calling " + ddcmd)
        subprocess.call(ddcmd.split())
        
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
        log("ERROR!: user denied download permission. please use '--tor-prefix' option.")
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

    while True:
        sys.stdout.write(question + prompt)
        choice = raw_input().lower()
        if default is not None and choice == '': return valid[default]
        elif choice in valid: return valid[choice]
        else: sys.stdout.write("Please respond with 'yes' or 'no' (or 'y' or 'n').\n")
    
def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] Shadow Setup: "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
