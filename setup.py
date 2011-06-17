#!/usr/bin/env python2.7

##
# The Shadow Simulator
#
# Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

import sys, os, argparse, subprocess, shutil, urllib2, tarfile
from datetime import datetime

BUILD_PREFIX="build"
INSTALL_PREFIX="/usr/local"

RESOURCES_URL="http://shadow.cs.umn.edu/shadow-resources.tar.gz"

def main():
    parser_main = argparse.ArgumentParser(description='Utility to help setup the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_main.add_argument('-q', '--quiet', action="store_true", dest="be_quiet",
          help="this script will not display its actions", default=False)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(help='run a subcommand (for help use <subcommand> --help)')
    
    # configure subcommand
    parser_build = subparsers_main.add_parser('build', help='configure and build the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_build.set_defaults(func=build, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    parser_build.add_argument('-p', '--prefix', action="store", dest="prefix",
          help="path to root directory for shadow installation", metavar="PATH", default=INSTALL_PREFIX)
    parser_build.add_argument('-i', '--include', action="append", dest="extra_includes", metavar="PATH",
          help="include PATH when searching for headers. useful if dependencies are installed to non-standard locations.")
    parser_build.add_argument('-l', '--library', action="append", dest="extra_libraries", metavar="PATH",
          help="include PATH when searching for libraries. useful if dependencies are installed to non-standard locations.")
    parser_build.add_argument('-g', '--debug', action="store_true", dest="do_debug",
          help="turn on debugging for verbose program output", default=False)
    parser_build.add_argument('-t', '--test', action="store_true", dest="do_test",
          help="build and run tests", default=False)
    parser_build.add_argument('-c', '--coverage', action="store_true", dest="do_coverage",
          help="get coverage statistics", default=False)
    parser_build.add_argument('-d', '--doc', action="store_true", dest="do_doc",
          help="generate documentation into BUILD-DIR/shadow/doc", default=False)
    
    # install subcommand
    parser_install = subparsers_main.add_parser('install', help='install the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_install.set_defaults(func=install, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    parser_auto = subparsers_main.add_parser('auto', help='build to ./build, install to local prefix. useful for quick local setup and during development.', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_auto.set_defaults(func=auto, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    default_prefix = os.path.abspath(os.getenv("HOME") + "/.local/")
    parser_auto.add_argument('-p', '--prefix', action="store", dest="prefix",
          help="install to PATH using libs from PATH/lib and includes from PATH/include", metavar="PATH", default=default_prefix)
    parser_auto.add_argument('-g', '--debug', action="store_true", dest="do_debug",
          help="turn on debugging for verbose program output", default=False)
        
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    # run chosen command
    args.func(args)

def build(args):
    outfile = get_outfile(args)
    
    filepath=os.path.abspath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=os.path.abspath(BUILD_PREFIX)
    shadowdir=os.path.abspath(BUILD_PREFIX + "/shadow")
    installdir=os.path.abspath(args.prefix)
    
    # clear cmake cache
    if(os.path.exists(shadowdir)): shutil.rmtree(shadowdir)
    if not os.path.exists(shadowdir): os.makedirs(shadowdir)
    if not os.path.exists(installdir): os.makedirs(installdir)

    # build up args string for cmake
    cmake_cmd = "cmake " + rootdir + " -DCMAKE_BUILD_PREFIX=" + shadowdir + " -DCMAKE_INSTALL_PREFIX=" + installdir
    
    if args.extra_includes is None: args.extra_includes = []
    if args.extra_libraries is None: args.extra_libraries = []
    
    # hack to make passing args to CMAKE work... doesnt seem to like the first arg
    args.extra_includes.insert(0, "./")
    args.extra_libraries.insert(0, "./")
    
    make_paths_absolute(args.extra_includes)
    make_paths_absolute(args.extra_libraries)

    cmake_cmd += " -DCMAKE_EXTRA_INCLUDES=" + ';'.join(args.extra_includes)
    cmake_cmd += " -DCMAKE_EXTRA_LIBRARIES=" + ';'.join(args.extra_libraries)
    if(args.do_coverage): cmake_cmd += " -DSHADOW_COVERAGE=ON"
    if(args.do_doc): cmake_cmd += " -DSHADOW_DOC=ON"
    if(args.do_debug): cmake_cmd += " -DSHADOW_DEBUG=ON"
    if(args.do_test): cmake_cmd += " -DSHADOW_TEST=ON"
    
    # we will run from build directory
    rundir = os.getcwd()
    os.chdir(builddir)
    get_resources(args)
    os.chdir(shadowdir)
    
    # call cmake to configure the make process, wait for completion
    log(args, "running \'" + cmake_cmd + "\' from " + shadowdir)
    retcode = subprocess.call(cmake_cmd.strip().split(), stdout=outfile)
    log(args, "cmake returned " + str(retcode))
    
    if retcode == 0:
        # call make, wait for it to finish
        log(args, "calling \'make\'")
        retcode = subprocess.call(["make"], stdout=outfile)
        log(args, "make returned " + str(retcode))
        if retcode == 0: log(args, "now run \'python setup.py install\'")
        else: log(args, "E! Non-zero return code.")

    # go back to where we came from
    os.chdir(rundir)
    return retcode

def install(args):
    outfile = get_outfile(args)
    
    shadowdir=os.path.abspath(BUILD_PREFIX + "/shadow")
    if not os.path.exists(shadowdir): 
        log(args, "ERROR: please build before installing!")
        return

    # go to build dir and install from makefile
    rundir = os.getcwd()
    os.chdir(shadowdir)
    
    # call make install, wait for it to finish
    log(args, "calling \'make install\'")
    retcode = subprocess.call(["make", "install"], stdout=outfile)
    log(args, "make install returned " + str(retcode))
    log(args, "run \'shadow\' from the install directory")
    
    # go back to where we came from
    os.chdir(rundir)
    return retcode

def auto(args):
    args.do_coverage = False
    args.do_doc = False
    args.do_test = False
    args.extra_includes = [os.path.abspath(args.prefix + "/include")]
    args.extra_libraries = [os.path.abspath(args.prefix + "/lib")]
    if build(args) == 0: install(args)

def get_resources(args):
    args.target_resources = os.path.abspath(os.path.basename(RESOURCES_URL))
    # download and extract
    if not os.path.exists(args.target_resources):
        log(args, "downloading " + RESOURCES_URL)
        if download(RESOURCES_URL, args.target_resources) != 0:
            log(args, "failed to download " + RESOURCES_URL)
            return -1
        
    resources_dir = args.target_resources[:args.target_resources.rindex(".tar.gz")]
    if tarfile.is_tarfile(args.target_resources) and not os.path.exists(resources_dir):
        tar = tarfile.open(args.target_resources, "r:gz")
        tar.extractall()
        tar.close()
    else: return -1

def get_outfile(args):
    # check if we redirect to null
    if(args.be_quiet): return open("/dev/null", 'w')
    else: return None
        
def make_paths_absolute(list):
    for i in xrange(len(list)): list[i] = os.path.abspath(list[i])
    
def download(url, target_path):
    try:
        u = urllib2.urlopen(url)
        localfile = open(target_path, 'w')
        localfile.write(u.read())
        localfile.close()
        return 0
    except urllib2.URLError:
        return -1

def log(args, msg):
    if not args.be_quiet:
        color_start_code = "\033[94m" # red: \033[91m"
        color_end_code = "\033[0m"
        prefix = "[" + str(datetime.now()) + "] setup: "
        print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
