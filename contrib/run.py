#!/usr/bin/python

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

import os, subprocess, shutil, gzip
from datetime import datetime
from time import sleep

def log(msg):
    print str(datetime.now()), msg

def dd(filename, kb):
    if not os.path.exists(filename):
        ddcmd = "/bin/dd if=/dev/urandom of=" + filename + " bs=1024 count=" + str(kb)
        log("calling " + ddcmd)
        subprocess.call(ddcmd.split())

def run(outdir, dsimpath, logfilepath):
    authdir=outdir+"/authoritydata"
    exitdir=outdir+"/exitdata"
    relaydir=outdir+"/relaydata"
    clientdir=outdir+"/clientdata"

    # clean
    if(os.path.exists(authdir)):
        shutil.rmtree(authdir)
    if(os.path.exists(exitdir)):
        shutil.rmtree(exitdir)
    if(os.path.exists(relaydir)):
        shutil.rmtree(relaydir)
    if(os.path.exists(clientdir)):
        shutil.rmtree(clientdir)

    # setup
    os.makedirs(authdir)
    os.makedirs(exitdir)
    os.makedirs(relaydir)
    os.makedirs(clientdir)
    shutil.copytree("config/authoritydata", authdir + "/4uthority.scallion.shd")

    # start monitoring
    dstat_cmd = "dstat -cmstTy --fs --output data/dstat.log"
    dstat_p = subprocess.Popen(dstat_cmd.split(), stdout=open("/dev/null", 'w'))

    # run
    os.putenv("LD_PRELOAD", "./libpreload.so:./libscallion_preload.so")
    cmd = "./shadow -p 1 -d " + dsimpath

    log("calling " + cmd + " with logfile " + logfilepath)

    f = open(logfilepath, 'w')
    start = datetime.now()
    retcode = subprocess.call(cmd.split(), stdout=f)
    end = datetime.now()
    f.close()

    dstat_p.kill()

    log(str(end) + " shadow returned " + str(retcode) + " in " + str(end-start) + " seconds")

# main
if not os.path.exists("www"):
    os.makedirs("www")
if not os.path.exists("data"):
    os.makedirs("data")
dd("www/320kb.urnd", 320)
dd("www/5mb.urnd", 5120)

run("data/", "topology/scallion.dsim", "data/scallion.log")
#sleep(1)
#run("data/ewma3", "run/ewma3.dsim", "data/ewma3.log")
#sleep(1)
#run("data/ewma30", "run/ewma30.dsim", "data/ewma30.log")
#sleep(1)
#run("data/ewma90", "run/ewma90.dsim", "data/ewma90.log")

log("all done!")

