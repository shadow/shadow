#!/usr/bin/env python2.7

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

import sys, os, argparse, subprocess, shutil
from datetime import datetime

BUILD_PREFIX="./build"
INSTALL_PREFIX=os.path.expanduser("~/.shadow")

def main():
    parser_main = argparse.ArgumentParser(description='Utility to help setup the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(help='run a subcommand (for help use <subcommand> --help)')
    
    # configure subcommand
    parser_build = subparsers_main.add_parser('build', help='configure and build the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_build.set_defaults(func=build, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    parser_build.add_argument('-p', '--prefix', action="store", dest="prefix",
          help="configure PATH as Shadow root installation directory", metavar="PATH", default=INSTALL_PREFIX)
    parser_build.add_argument('-i', '--include', action="append", dest="extra_includes", metavar="PATH",
          help="append PATH to the list of paths searched for headers. useful if dependencies are installed to non-standard locations.",
          default=[INSTALL_PREFIX+ "/include"])
    parser_build.add_argument('-l', '--library', action="append", dest="extra_libraries", metavar="PATH",
          help="append PATH to the list of paths searched for libraries. useful if dependencies are installed to non-standard locations.",
          default=[INSTALL_PREFIX+ "/lib"])
    parser_build.add_argument('-g', '--debug', action="store_true", dest="do_debug",
          help="define DEBUG during build, useful if you want extra memory checks when running Shadow", default=False)
    parser_build.add_argument('-t', '--test', action="store_true", dest="do_test",
          help="build tests", default=False)
    
    # install subcommand
    parser_install = subparsers_main.add_parser('install', help='install Shadow', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_install.set_defaults(func=install, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    # run chosen command
    args.func(args)

def build(args):
    filepath=os.path.abspath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=os.path.abspath(BUILD_PREFIX)
    installdir=os.path.abspath(args.prefix)
    
    # clear cmake cache
    if(os.path.exists(builddir)): shutil.rmtree(builddir)
    
    # create directories
    if not os.path.exists(builddir): os.makedirs(builddir)
    if not os.path.exists(installdir): os.makedirs(installdir)

    # build up args string for cmake
    cmake_cmd = "cmake " + rootdir + " -DCMAKE_BUILD_PREFIX=" + builddir + " -DCMAKE_INSTALL_PREFIX=" + installdir
    
    if args.extra_includes is None: args.extra_includes = []
    if args.extra_libraries is None: args.extra_libraries = []
    
    # hack to make passing args to CMAKE work... doesnt seem to like the first arg
    args.extra_includes.insert(0, "./")
    args.extra_libraries.insert(0, "./")
    
    # add extra library and include directories as absolution paths
    make_paths_absolute(args.extra_includes)
    cmake_cmd += " -DCMAKE_EXTRA_INCLUDES=" + ';'.join(args.extra_includes)
    make_paths_absolute(args.extra_libraries)
    cmake_cmd += " -DCMAKE_EXTRA_LIBRARIES=" + ';'.join(args.extra_libraries)

    # other cmake options
    if args.do_debug: cmake_cmd += " -DSHADOW_DEBUG=ON"
    if args.do_test: cmake_cmd += " -DSHADOW_TEST=ON"
    
    # we will run from build directory
    calledDirectory = os.getcwd()
    os.chdir(builddir)
    
    # call cmake to configure the make process, wait for completion
    log("running \'" + cmake_cmd + "\' from " + builddir)
    retcode = subprocess.call(cmake_cmd.strip().split())
    log("cmake returned " + str(retcode))
    
    if retcode == 0:
        # call make, wait for it to finish
        log("calling \'make\'")
        retcode = subprocess.call(["make"])
        log("make returned " + str(retcode))
        if retcode == 0: log("now run \'python setup.py install\'")
        else: log("E! Non-zero return code.")

    # go back to where we came from
    os.chdir(calledDirectory)
    return retcode

def install(args):
    builddir=os.path.abspath(BUILD_PREFIX)
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
    log("run \'shadow\' from the install directory")
    
    # go back to where we came from
    os.chdir(calledDirectory)
    return retcode

def make_paths_absolute(list):
    for i in xrange(len(list)): list[i] = os.path.abspath(list[i])
    
def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] Shadow Setup: "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
