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

import os, sys, subprocess, shutil, gzip, argparse, pygeoip, random, socket
from datetime import datetime
from time import sleep

# This should NOT be expanded, we'll use this directly in the XML file
INSTALLPREFIX="~/.shadow/"

NRELAYS = 10
FEXIT = 0.4
NCLIENTS = 300
FBULK = 0.05
NSERVERS = 30

class Relay():
    def __init__(self, ip, bw, isExit=False):
        self.ip = ip
        self.bw = bw
        self.isExit = isExit
        self.code = None
        self.bwrate = None
        self.bwburst = None
        self.bwhistory = None
        
    def setTokenBucketBW(self, bwrate, bwburst, bwhistory):
        self.bwrate = bwrate
        self.bwburst = bwburst
        self.bwhistory = bwhistory
        
    def setRegionCode(self, code):
        self.code = code

    def toCSV(self):
        return ",".join([self.ip, self.code, str(self.isExit), str(self.bw), str(self.bwrate), str(self.bwburst), str(self.bwhistory)])

def main():
    ap = argparse.ArgumentParser(description='Generate hosts.xml file for Scallion Tor experiments in Shadow', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        
    # configuration options
    ap.add_argument('-p', '--prefix', action="store", dest="prefix", help="PATH to base Shadow installation", metavar="PATH", default=INSTALLPREFIX)
    ap.add_argument('--nrelays', action="store", type=int, dest="nrelays", help="number N of total relays for the generated topology", metavar='N', default=NRELAYS)
    ap.add_argument('--fexit', action="store", type=float, dest="exitfrac", help="fraction F of relays that are exits", metavar='F', default=FEXIT)
    ap.add_argument('--nclients', action="store", type=int, dest="nclients", help="number N of total clients for the generated topology", metavar='N', default=NCLIENTS)
    ap.add_argument('--fbulk', action="store", type=float, dest="fbulk", help="fraction F of bulk downloading clients (remaining are web clients)", metavar='F', default=FBULK)
    ap.add_argument('--nservers', action="store", type=int, dest="nservers", help="number N of fileservers", metavar='N', default=NSERVERS)

    # positional args (required)
    ap.add_argument('consensus', action="store", type=str, help="PATH to a current Tor consensus file", metavar='PATH', default=None)

    # get arguments, accessible with args.value
    args = ap.parse_args()
    
    generate(args)
    log("finished generating {0}/hosts.xml".format(os.getcwd()))

def generate(args):
    # get list of relays, sorted by increasing bandwidth
    relays = parse_consensus(args.consensus)
    
    # separate out exits and nonexits
    exits, nonexits = [], []
    for relay in relays:
        if relay.isExit: exits.append(relay)
        else: nonexits.append(relay)
        
    # sample for the exits and nonexits we'll use for our nodes
    nexits = int(args.exitfrac * args.nrelays)
    exitnodes = sample_relays(exits, nexits)
    nnonexits = args.nrelays - nexits
    nonexitnodes = sample_relays(nonexits, nnonexits)
    
# relays should be sorted by bandwidth
def sample_relays(relays, k):
    # sample k of n relays to best fit the original list
    n = len(relays)
    assert k < n
    
    t = 0
    bins = []
    for i in range(k):
        bin = []
        for j in range(n/k):
            bin.append(relays[t])
            t += 1
        bins.append(bin)
    
    sample = []
    for bin in bins: sample.append(bin[len(bin)/2])
    
    return sample
    
def parse_consensus(consensus_path):
    relays = []
    ip = ""
    bw = 0.0
    isExit = False
    
    with open(consensus_path) as f:
        for line in f:
            if line[0:2] == "r ":
                # append the relay that we just built up
                if ip != "": 
                    r = Relay(ip, bw, isExit)                 
                    relays.append(r)
                # reset for the next relay
                bw = 0.0
                isExit = False
                ip = line.strip().split()[6]
            elif line[0:2] == "s ":
                if line.strip().split()[1] == "Exit": isExit = True
            elif line[0:2] == "w ":
                bw = float(line.strip().split()[1].split("=")[1])
    
    return sorted(relays, key=lambda relay: relay.bw)

def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] scallion: "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
