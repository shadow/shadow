#!/usr/bin/env python

import sys, os, argparse, subprocess
from datetime import datetime

# path to the built-in example plug-ins relative to the shadow base directory
ECHO_PATH="src/plug-ins/echo"
PINGPONG_PATH="src/plug-ins/pingpong"
FILETRANSFER_PATH="src/plug-ins/filetransfer"

# path to the preload lib relative to the cmake dir
PRELOAD_PATH="lib/libshadow-preload.so"

# path to the shadow binary relative to the cmake dir
SHADOW_PATH="bin/shadow-bin"

# path to other programs
TIME_PATH="/usr/bin/time"
VALGRIND_PATH="/usr/bin/valgrind"

def main():
    parser_main = argparse.ArgumentParser(description='Utility to help build and run the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_main.add_argument('-s', '--shadow-dir', action="store", dest="shadow_directory",
          help="path to the shadow base directory", default="./")
    parser_main.add_argument('-b', '--build-dir', action="store", dest="build_directory",
          help="generate build output into BUILD-DIR", default="./build")
    parser_main.add_argument('-i', '--install-dir', action="store", dest="install_directory",
          help="directory where shadow will be installed if directed", default="/usr/local")
    parser_main.add_argument('-q', '--quiet', action="store_true", dest="be_quiet",
          help="this script will not display its actions", default=False)
    
    # @todo: refactor - many of the arguments are shared among subparsers
    # setup our commands
    subparsers_main = parser_main.add_subparsers(help='run a subcommand (for help use <subcommand> --help)')
    
    # build subcommand
    parser_build = subparsers_main.add_parser('build', help='build the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_build.set_defaults(func=do_build, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # build options
    parser_build.add_argument('-r', '--rebuild', action="store_true", dest="do_rebuild", 
          help="force a rebuild, removing cached build files located in BUILD-DIR/", default=False)
    parser_build.add_argument('-g', '--debug', action="store_true", dest="do_debug",
          help="turn on debugging for verbose program output", default=False)
    parser_build.add_argument('-t', '--test', action="store_true", dest="do_test",
          help="build and run tests", default=False)
    parser_build.add_argument('-c', '--coverage', action="store_true", dest="do_coverage",
          help="get coverage statistics", default=False)
    parser_build.add_argument('-d', '--doc', action="store_true", dest="do_doc",
          help="generate documentation into BUILD-DIR/shadow/doc", default=False)
    parser_build.add_argument('-q', '--quiet', action="store_true", dest="no_stdout",
          help="redirect stdout to /dev/null", default=False)

    # install subcommand
    parser_install = subparsers_main.add_parser('install', help='install the shadow simulator as configured during build', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_install.set_defaults(func=do_install, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # install options
    parser_install.add_argument('-q', '--quiet', action="store_true", dest="no_stdout",
          help="redirect stdout to /dev/null", default=False)

    # run subcommand
    parser_run = subparsers_main.add_parser('run', help='run the shadow simulator', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_run.set_defaults(func=do_run, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # run options
    parser_run.add_argument('-c', '--config', action="store", dest="config_file",
          help="path to custom shadow configuration options")
    parser_run.add_argument('-d', '--dsim', action="store", dest="dsim_file",
          help="path to custom simulation description (dsim) script", required=True)
    parser_run.add_argument('-p', '--processes', type=int, action="store", dest="num_processes", choices=[1,2,3,4,5,6,7,8,9,10,11,12],
          help="number of worker processes, in addition to the master process, to use in the simulation", default=1)
    parser_run.add_argument('-l', '--ld-preload', action="append", dest="extra_preloads",
          help="add a library to the list of libraries that will be preloaded before running shadow")
    parser_run.add_argument('-g', '--valgrind', action="store_true", dest="use_valgrind",
          help="run under the valgrind memory checker", default=False)
    parser_run.add_argument('-q', '--quiet', action="store_true", dest="no_stdout",
          help="redirect stdout to /dev/null", default=False)
    parser_run.add_argument('--dry-run', action="store_true", dest="do_dry_run",
          help="do everything except invoking shadow", default=False)
        
    # autorun command
    parser_autorun = subparsers_main.add_parser('autorun', help='run the shadow simulator using default plug-ins and dsim files', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_autorun.set_defaults(func=do_autorun, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # autorun options
    parser_autorun.add_argument('-p', '--processes', type=int, action="store", metavar="N", dest="num_processes", choices=[1,2,3,4,5,6,7,8,9,10,11,12],
          help="number of worker processes, in addition to the master process, to use in the simulation", default=1)
    parser_autorun.add_argument('-q', '--quiet', action="store_true", dest="no_stdout",
          help="redirect stdout to /dev/null", default=False)
    
    # default built-in examples
    group_autorun = parser_autorun.add_mutually_exclusive_group(required=True)
    group_autorun.add_argument('--echo-eth', action="store_true", dest="run_echo_eth", 
          help="run the echo example with a client and server over ethernet", default=False)
    group_autorun.add_argument('--echo-lo', action="store_true", dest="run_echo_loop", 
          help="run the echo example with a client and server over loopback", default=False)
    group_autorun.add_argument('--pingpong-tcp', action="store_true", dest="run_pingpong_tcp", 
          help="run the ping-pong example using tcp", default=False)
    group_autorun.add_argument('--pingpong-udp', action="store_true", dest="run_pingpong_udp", 
          help="run the ping-pong example using udp", default=False)
    group_autorun.add_argument('--filetransfer', action="store_true", dest="run_filetransfer", 
          help="run filetransfer example with clients and a server over ethernet", default=False)
    
    # get arguments, accessible with args.value
    args = parser_main.parse_args()
    # run chosen command
    args.func(args)
    
def do_build(args):
    set_absolute_paths(args)
    outfile = get_outfile(args)

    # prepare filesystem
    prepare(args)
    
    # configure
    retcode = setup(args, outfile)

    # if we have success, we can make
    if(retcode == 0): retcode = build(args, outfile)

    log_result(args, "building shadow and plug-ins", retcode)

def prepare(args):
    # remove and or make directories
    if(args.do_rebuild and os.path.exists(args.build_directory)):
        import shutil
        shutil.rmtree(args.build_directory)
    if not os.path.exists(args.build_directory):
        os.makedirs(args.build_directory)
    if not os.path.exists(args.install_directory):
        os.makedirs(args.install_directory)

def setup(args, output):
    # start building up args string for cmake
    # we will run from build directory
    rundir = os.getcwd()

    cmake_args = []
    cmake_args.append("cmake")
    cmake_args.append(args.shadow_directory)
    cmake_args.append("-DCMAKE_INSTALL_PREFIX=" + args.install_directory)
    cmake_args.append("-DCMAKE_BUILD_PREFIX=" + args.build_directory)

    if(args.do_coverage):
        cmake_args.append("-DSHADOW_COVERAGE=ON")
    
    if(args.do_doc):
        cmake_args.append("-DSHADOW_DOC=ON")
    
    if(args.do_debug):
        cmake_args.append("-DSHADOW_DEBUG=ON")
        
    if(args.do_test):
        cmake_args.append("-DSHADOW_TEST=ON")
        
    # go to build dir to run cmake to build shadow
    os.chdir(args.build_directory)
    log_list(args, "running cmake from " + args.build_directory + "\n\t", cmake_args)
    
    # call cmake to configure the make process, wait for completion
    retcode = subprocess.call(cmake_args, stdout=output)
    log_result(args, "cmake", retcode)

    # go back to where we came from
    os.chdir(rundir)
    return retcode

def build(args, output):
    # go to build dir and run make from makefile
    rundir = os.getcwd()
    os.chdir(args.build_directory)
    
    # call make, wait for it to finish
    log(args, "calling \'make\'")
    retcode = subprocess.call(["make"], stdout=output)
    log_result(args, "make", retcode)
    
    # go back to where we came from
    os.chdir(rundir)
    return retcode

def do_install(args):
    set_absolute_paths(args)
    outfile = get_outfile(args)

    if not os.path.exists(args.build_directory): 
        log_fail(args, "please build before installing! make install")
        return

    retcode = install(args, outfile)

def install(args, output):
    # go to build dir and install from makefile
    rundir = os.getcwd()
    os.chdir(args.build_directory)
    
    # call make, wait for it to finish
    log(args, "calling \'make install\'")
    retcode = subprocess.call(["make", "install"], stdout=output)
    log_result(args, "make install", retcode)
    
    # go back to where we came from
    os.chdir(rundir)
    return retcode

def do_run(args):
    set_absolute_paths(args)
    run(args)

def run(args):
    # start with the default preload
    preloads = args.build_directory + '/' + PRELOAD_PATH
    # add user-specified preloads
    if args.extra_preloads is not None:
        for user_preload in args.extra_preloads:
            preloads += ":" + os.path.abspath(user_preload.strip())
    
    os.putenv("LD_PRELOAD", preloads)
    
    log(args, "set environmental variable: LD_PRELOAD=" + preloads)
        
    rundir = os.getcwd()
    os.chdir(args.shadow_directory)
    
    shadow = args.build_directory + "/" + SHADOW_PATH
    
    # use default built-in config by not specifying a "-c" option, unless they passed a config file
    shadow_args = [shadow, "-p", str(args.num_processes), "-d", args.dsim_file]
    if(args.config_file is not None):
        shadow_args.append("-c")
        shadow_args.append(args.config_file)
    
    # prepending with either time or valgrind
    run_args = None
    if(args.use_valgrind):
        run_args = [VALGRIND_PATH, "--leak-check=full", "--show-reachable=yes", "--track-origins=yes", "--trace-children=yes", "--log-file=valgrind-shadow-%p.log", "--error-limit=no"]
        log(args, "logging valgrind output to logfile")
    else: 
        run_args = [TIME_PATH]
        
    run_args.extend(shadow_args)
    log_list(args, "running shadow from\n\t" + args.shadow_directory + "\nusing args\n\t", run_args)
    
    if(not args.do_dry_run):
        retcode = subprocess.call(run_args, stdout=get_outfile(args))
        log_result(args, "run", retcode)
    
    os.chdir(rundir)
 
def do_autorun(args):
    set_absolute_paths(args)
    autorun(args)
       
def autorun(args):
    # default preloads depend what we are running
    preloads = args.build_directory + '/' + PRELOAD_PATH
    dsim_path = ""

    if(args.run_echo_eth):
        dsim_path = os.path.abspath(ECHO_PATH + "/echo-ethernet.dsim")
    elif(args.run_echo_loop):
        dsim_path = os.path.abspath(ECHO_PATH + "/echo-loopback.dsim")
    elif(args.run_pingpong_tcp):
        dsim_path = os.path.abspath(PINGPONG_PATH + "/pingpong-tcp.dsim")
    elif(args.run_pingpong_udp):
        dsim_path = os.path.abspath(PINGPONG_PATH + "/pingpong-udp.dsim")
    elif(args.run_filetransfer):
        dsim_path = os.path.abspath(FILETRANSFER_PATH + "/filetransfer.dsim")
        
    os.putenv("LD_PRELOAD", preloads)
    log(args, "set environmental variable: LD_PRELOAD=" + preloads)
        
    rundir = os.getcwd()
    os.chdir(args.shadow_directory)
    
    shadow = args.build_directory + "/" + SHADOW_PATH
    
    # use default built-in config by not specifying a "-c" option
    run_args = [TIME_PATH, shadow, "-p", str(args.num_processes), "-d", dsim_path]
    log_list(args, "running shadow from\n\t" + args.shadow_directory + "\nusing args\n\t", run_args)
    
    retcode = subprocess.call(run_args, stdout=get_outfile(args))
    log_result(args, "autorun", retcode)
    
    os.chdir(rundir)
    
def set_absolute_paths(args):
    # make sure we have a full paths
    args.shadow_directory = os.path.abspath(args.shadow_directory)
    args.build_directory = os.path.abspath(args.build_directory)
    args.install_directory = os.path.abspath(args.install_directory)
    
def get_outfile(args):
    # check if we redirect to null
    if(args.no_stdout): return open("/dev/null", 'w')
    else: return None
    
def log(args, msg):
    log_helper(args, msg, "\033[94m")
    
def log_result(args, msg, retcode):
    if(retcode == 0): log_success(args, msg)
    else: log_fail(args, msg)

def log_success(args, msg):
    log_helper(args, msg + ": SUCCEEDED!", "\033[92m")
    
def log_fail(args, msg):
    log_helper(args, msg + ": FAILED!", "\033[91m")
    
def log_helper(args, msg, color_start_code):
    if(not args.be_quiet):
        prefix = "[" + str(datetime.now()) + "] shadowrunner.py: "
        print >> sys.stderr, color_start_code + prefix + msg + "\033[0m"
        
def log_list(args, msg, list):
    for item in list: msg += item + " "
    log(args, msg)

if __name__ == '__main__':
    main()
