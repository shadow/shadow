#!/usr/bin/env python

import sys, os, argparse, subprocess
from datetime import datetime

BUILD_PREFIX="build"
INSTALL_PREFIX = os.getenv("HOME") + "/.local"

def main():
    parser_main = argparse.ArgumentParser(description='Utility to help setup the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_main.add_argument('-q', '--quiet', action="store_true", dest="be_quiet",
          help="this script will not display its actions", default=False)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(help='run a subcommand (for help use <subcommand> --help)')
    
    # configure subcommand
    parser_build = subparsers_main.add_parser('build', help='configure and build the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_build.set_defaults(func=build, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    parser_build.add_argument('-i', '--install-prefix', action="store", dest="prefix",
          help="path to root directory for shadow installation", default=INSTALL_PREFIX)
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
    
    parser_auto = subparsers_main.add_parser('auto', help='build to ./build, install to ./install. useful for quick local setup during development.', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_auto.set_defaults(func=auto, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    # run chosen command
    args.func(args)
    
def get_outfile(args):
    # check if we redirect to null
    if(args.be_quiet): return open("/dev/null", 'w')
    else: return None

def build(args):
    outfile = get_outfile(args)
    
    filepath=os.path.abspath(__file__)
    rootdir=filepath[:filepath.rfind("/")]
    builddir=os.path.abspath(BUILD_PREFIX)
    installdir=os.path.abspath(args.prefix)
    
    # clear cmake cache
    if(os.path.exists(builddir)):
        import shutil
        shutil.rmtree(builddir)
    if not os.path.exists(builddir): os.makedirs(builddir)
    if not os.path.exists(installdir): os.makedirs(installdir)

    # build up args string for cmake
    cmake_cmd = "cmake " + rootdir + " -DCMAKE_BUILD_PREFIX=" + builddir + " -DCMAKE_INSTALL_PREFIX=" + installdir

    if(args.do_coverage): cmake_cmd += " -DSHADOW_COVERAGE=ON"
    if(args.do_doc): cmake_cmd += " -DSHADOW_DOC=ON"
    if(args.do_debug): cmake_cmd += " -DSHADOW_DEBUG=ON"
    if(args.do_test): cmake_cmd += " -DSHADOW_TEST=ON"
    
    # we will run from build directory
    rundir = os.getcwd()
    os.chdir(builddir)
    
    # call cmake to configure the make process, wait for completion
    log(args, "running \'" + cmake_cmd + "\' from " + builddir)
    retcode = subprocess.call(cmake_cmd.strip().split(), stdout=outfile)
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

    # go to build dir and install from makefile
    rundir = os.getcwd()
    os.chdir(builddir)
    
    # call make install, wait for it to finish
    log(args, "calling \'make install\'")
    retcode = subprocess.call(["make", "install"], stdout=outfile)
    log(args, "make install returned " + str(retcode))
    log(args, "run \'shadow\' from the install directory")
    
    # go back to where we came from
    os.chdir(rundir)
    return retcode

def auto(args):
    args.prefix = "./install"
    args.do_coverage = False
    args.do_doc = False
    args.do_debug = False
    args.do_test = False
    if build(args) == 0: install(args)

def log(args, msg):
    if not args.be_quiet:
        color_start_code = "\033[94m" # red: \033[91m"
        color_end_code = "\033[0m"
        prefix = "[" + str(datetime.now()) + "] setup: "
        print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
