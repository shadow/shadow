#!/usr/bin/env python2.7

##
# Scallion - plug-in for The Shadow Simulator
#
# Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
#
# This file is part of Scallion.
#
# Scallion is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Scallion is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Scallion.  If not, see <http://www.gnu.org/licenses/>.
#

import sys, os, argparse, subprocess, shlex, shutil, urllib2, tarfile, gzip, stat
from datetime import datetime

BUILD_PREFIX="./build"
INSTALL_PREFIX=os.path.expanduser("~/.shadow")

DEFAULT_TOR_VERSION="0.2.3.13-alpha"

TOR_URL="https://archive.torproject.org/tor-package-archive/tor-" + DEFAULT_TOR_VERSION + ".tar.gz"
MAXMIND_URL="http://geolite.maxmind.com/download/geoip/database/GeoLiteCity.dat.gz"

PREPARETOR=os.path.abspath(BUILD_PREFIX+"/preparetor")
READYTOR=os.path.abspath(BUILD_PREFIX+"/readytor")

def main():
    parser_main = argparse.ArgumentParser(description='Utility to help setup the scallion plug-in for the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_main.add_argument('-q', '--quiet', action="store_true", dest="be_quiet",
          help="this script will not display its actions", default=False)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(help='run a subcommand (for help use <subcommand> --help)')
    
    # configure subcommand
    parser_build = subparsers_main.add_parser('build', help='configure and build scallion', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_build.set_defaults(func=build, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    parser_build.add_argument('-p', '--prefix', action="store", dest="prefix",
          help="path to root directory for scallion installation", metavar="PATH", default=INSTALL_PREFIX)
    parser_build.add_argument('-i', '--include', action="append", dest="extra_includes", metavar="PATH",
          help="include PATH when searching for headers. useful if dependencies are installed to non-standard locations.",
          default=[INSTALL_PREFIX+ "/include", os.path.expanduser("~/.local/include")])
    parser_build.add_argument('-l', '--library', action="append", dest="extra_libraries", metavar="PATH",
          help="include PATH when searching for libraries. useful if dependencies are installed to non-standard locations.",
          default=[INSTALL_PREFIX+ "/lib", os.path.expanduser("~/.local/lib")])
    parser_build.add_argument('--tor-prefix', action="store", dest="prefix_tor", metavar="PATH",
          help="PATH to base Tor directory to build. this overrides the remote download triggered with '-v' or '--version' options.", default=None)
    parser_build.add_argument('--libevent-prefix', action="store", dest="prefix_libevent", metavar="PATH",
          help="use non-standard PATH when linking Tor to libevent.", default=INSTALL_PREFIX)
    parser_build.add_argument('--openssl-prefix', action="store", dest="prefix_openssl", metavar="PATH",
          help="use non-standard PATH when linking Tor to openssl.", default=INSTALL_PREFIX)
    parser_build.add_argument('-g', '--debug', action="store_true", dest="do_debug",
          help="turn on debugging for verbose program output", default=False)
    parser_build.add_argument('-v', '--version', action="store", dest="tor_version",
          help="specify what version of Tor to download and build. not used if '--tor-prefix' is specified.", default=DEFAULT_TOR_VERSION)
    
    # install subcommand
    parser_install = subparsers_main.add_parser('install', help='install scallion', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_install.set_defaults(func=install, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    
    # run chosen command
    args.func(args)
    
def get_outfile(args):
    # check if we redirect to null
    if(args.be_quiet): return open("/dev/null", 'w')
    else: return None
    
def make_paths_absolute(list):
    for i in xrange(len(list)): list[i] = os.path.abspath(list[i])

def build(args):
    outfile = get_outfile(args)
    
    args.tor_url = TOR_URL.replace(DEFAULT_TOR_VERSION, args.tor_version)
    
    filepath=os.path.abspath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=os.path.abspath(BUILD_PREFIX)
    installdir=os.path.abspath(args.prefix)
    if args.prefix_tor is not None: args.prefix_tor = os.path.abspath(args.prefix_tor)
    
    # clear cmake cache
    if(os.path.exists(builddir+"/scallion")): shutil.rmtree(builddir+"/scallion")
    if not os.path.exists(builddir+"/scallion"): os.makedirs(builddir+"/scallion")
    if not os.path.exists(installdir): os.makedirs(installdir)
    
    # we will run from build directory
    rundir = os.getcwd()
    os.chdir(builddir)

    if gen_www_files(args) != 0: return
    if setup_dependencies(args) != 0: return
    if setup_tor(args) != 0: return
    include_tor(args)
    torversion = get_tor_version(args)
    if torversion == None: return
    
    log(args, "detected tor version {0}".format(torversion))
    
    os.chdir(builddir+"/scallion")

    # build up args string for cmake
    vparts = torversion.split(".")
    a, b, c, d = int(vparts[0]), int(vparts[1]), int(vparts[2]), int(vparts[3].split("-")[0])

    cmake_cmd = "cmake " + rootdir + " -DCMAKE_BUILD_PREFIX=" + builddir + " -DCMAKE_INSTALL_PREFIX=" + installdir + " -DTORPATH=" + READYTOR
    if c >= 3 and d >= 5: 
        cmake_cmd += " -DDOREFILL=1"
        log(args, "configured to use refill callbacks")
    
    if args.extra_includes is None: args.extra_includes = []
    if args.extra_libraries is None: args.extra_libraries = []
    
    # hack to make passing args to CMAKE work... doesnt seem to like the first arg
    args.extra_includes.insert(0, "./")
    args.extra_libraries.insert(0, "./")
    
    make_paths_absolute(args.extra_includes)
    make_paths_absolute(args.extra_libraries)
    
    cmake_cmd += " -DCMAKE_EXTRA_INCLUDES=\"" + ';'.join(args.extra_includes) + "\""
    cmake_cmd += " -DCMAKE_EXTRA_LIBRARIES=\"" + ';'.join(args.extra_libraries) + "\""
    if(args.do_debug): cmake_cmd += " -DSCALLION_DEBUG=ON"
    
    # call cmake to configure the make process, wait for completion
    log(args, "running \'" + cmake_cmd + "\' from " + builddir)
    retcode = subprocess.call(shlex.split(cmake_cmd), stdout=outfile)
    log(args, "cmake returned " + str(retcode))
    
    if retcode == 0:
        # call make, wait for it to finish
        log(args, "calling \'make\'")
        retcode = subprocess.call(["make"], stdout=outfile)
        log(args, "make returned " + str(retcode))
        log(args, "now run \'python setup.py install\'")

    # go back to where we came from
    os.chdir(rundir)
    return retcode

def install(args):
    outfile = get_outfile(args)
    
    builddir=os.path.abspath(BUILD_PREFIX)
    if not os.path.exists(builddir): 
        log(args, "ERROR: please build before installing!")
        return

    # patching CMakeFile.txt to look into correct tor directory
#    cmake_file = open("CMakeLists.txt.new","w")
#    patch_cmd = "sed 's/build\/tor-.*\/src/build\/tor-" + args.tor_version + "\/src/g' CMakeLists.txt"
#    retcode = subprocess.call(shlex.split(patch_cmd), stdout=cmake_file)
#    shutil.move(cmake_file.name, "CMakeLists.txt")
#    cmake_file.close()

    # go to build dir and install from makefile
    rundir = os.getcwd()
    os.chdir(builddir+"/scallion")
    
    # call make install, wait for it to finish
    log(args, "calling \'make install\'")
    retcode = subprocess.call(["make", "install"], stdout=outfile)
    log(args, "make install returned " + str(retcode))
    #if retcode == 0: log(args, "run \'shadow -d src/scallion.dsim -l build/scallion/src/libshadow-plugin-scallion-preload.so\'")
    
    # go back to where we came from
    os.chdir(rundir)
    return retcode
    
def setup_tor(args):
    rundir = os.getcwd()
    outfile = get_outfile(args)

    build = "make clean && make"
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

            if args.prefix_libevent is not None: configure += " --with-libevent-dir=" + os.path.abspath(args.prefix_libevent)
            if args.prefix_openssl is not None: configure += " --with-openssl-dir=" + os.path.abspath(args.prefix_openssl)
            
            if retcode == 0:
                # generate configure
                log(args, gen)
                for cmd in gen.split('&&'):
                    retcode = retcode | subprocess.call(shlex.split(cmd.strip()), stdout=outfile)
            
            # need to patch AFTER generating configure to avoid overwriting the patched configure
            if retcode == 0:
                #patch
                log(args, patch)
                retcode = subprocess.call(shlex.split(patch), stdout=outfile)
            
            if retcode == 0:
                # configure
                log(args, configure)
                retcode = subprocess.call(shlex.split(configure), stdout=outfile)

        # configure done now
        if retcode == 0:
            # build
            log(args, build)
            for cmd in build.split('&&'):
                retcode = retcode | subprocess.call(shlex.split(cmd.strip()), stdout=outfile)
    
        os.chdir(args.tordir)

        convert = "python static_symbol_converter.py " + args.tor_version
        if retcode == 0:
            log(args, convert)
            retcode = subprocess.call(shlex.split(convert), stdout=outfile)

        if retcode == 0:
            # build
            retcode = subprocess.call(shlex.split("make"), stdout=outfile)

        os.chdir(rundir)
        
        # if we had a failure, start over with patching
        if retcode != 0:
            #shutil.rmtree(args.tordir)
            return -1
        
    if os.path.exists(READYTOR): shutil.rmtree(READYTOR)
    shutil.copytree(args.tordir, READYTOR)
    args.tordir = READYTOR
    
    return retcode

def get_tor_version(args):
    # get tor version info
    orconf = os.path.abspath(args.tordir+"/orconfig.h")
    if not os.path.exists(orconf): 
        log(args, "ERROR: file '{0}' does not exist".format(orconf))
        return None
    
    search = "#define VERSION "
    
    with open(orconf, 'rb') as f:
        for line in f:
            if line.find(search) > -1:
                return line[line.index("\"")+1:line.rindex("\"")]
               
    log(args, "ERROR: cant find version information in config file '{0}'".format(orconf)) 
    return None

def include_tor(args):
    if args.extra_includes is None: args.extra_includes = []
    args.extra_includes.extend([args.tordir, args.tordir+"/src/or", args.tordir+"/src/common"])
    
    if args.extra_libraries is None: args.extra_libraries = []
    args.extra_libraries.extend([args.tordir, args.tordir+"/src/or", args.tordir+"/src/common"])
    
def setup_dependencies(args):
    outfile = get_outfile(args)
    
    log(args, "checking tor dependencies...")
    
    args.tordir = PREPARETOR
    
    # if local prefix given, always blow away our prepared directory
    if args.prefix_tor is not None:
        if os.path.exists(args.tordir): shutil.rmtree(args.tordir)
        shutil.copytree(args.prefix_tor, args.tordir)
        return 0
    
    # no local prefix. only download if our prepare directory does not exist
    if os.path.exists(args.tordir): return 0
    
    # we have no local directory, check requested url
    args.target_tor = os.path.abspath(os.path.basename(args.tor_url))

    # only download if we dont have the requested URL
    if not os.path.exists(args.target_tor):
        if download(args, args.tor_url, args.target_tor) != 0:
            return -1
    
    # extract it to our prepare dir
    if tarfile.is_tarfile(args.target_tor):
        tar = tarfile.open(args.target_tor, "r:gz")
        n = tar.next().name
        while n.find('/') < 0: n = tar.next().name
        d = os.path.abspath(os.getcwd() + "/" + n[:n.index('/')])
        tar.extractall()
        tar.close()
        shutil.copytree(d, args.tordir)
        shutil.rmtree(d)
    else: return -1

    return 0

def gen_www_files(args):
    dd(args, "1KiB.urnd", 1)
    dd(args, "16KiB.urnd", 16)
    dd(args, "32KiB.urnd", 32)
    dd(args, "50KiB.urnd", 50)
    dd(args, "320KiB.urnd", 320)
    dd(args, "1MiB.urnd", 1024)
    dd(args, "5MiB.urnd", 5120)
    return 0
    
def dd(args, filename, kb):
    if not os.path.exists(filename):
        ddcmd = "/bin/dd if=/dev/urandom of=" + filename + " bs=1024 count=" + str(kb)
        log(args, "calling " + ddcmd)
        subprocess.call(ddcmd.split())
        
def download(args, url, target_path):
    if query_yes_no("May we download '{0}'?".format(url)):
        log(args, "attempting to download " + url)
        try:
            u = urllib2.urlopen(url)
            localfile = open(target_path, 'w')
            localfile.write(u.read())
            localfile.close()
            log(args, "successfully downloaded '{0}'".format(url))
            return 0
        except urllib2.URLError:
            log(args, "failed to download '{0}'".format(url))
            return -1
    else:
        log(args, "user denied download permission")
        ## TODO allow them to give a local path to the download
        log(args, "supplying a local path is not yet supported, failing")
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

def log(args, msg):
    if not args.be_quiet:
        color_start_code = "\033[94m" # red: \033[91m"
        color_end_code = "\033[0m"
        prefix = "[" + str(datetime.now()) + "] setup: "
        print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
