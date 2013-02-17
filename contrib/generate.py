#!/usr/bin/env python2.7

##
# Scallion - Tor plug-in for The Shadow Simulator
#
# Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

import os, sys, subprocess, argparse, socket, time, math
from random import choice
from datetime import datetime
from numpy import mean
from lxml import etree
from lxml.html.builder import INS

# This should NOT be expanded, we'll use this directly in the XML file
INSTALLPREFIX="~/.shadow/"

# distribution of CPU frequencies, in KHz
CPUFREQS=["2200000", "2400000", "2600000", "2800000", "3000000", "3200000", "3400000"]

NRELAYS = 10
FEXIT = 0.4
NCLIENTS = 100
FIM = 0.02
FWEB = 0.89
FBULK = 0.04
FP2P = 0.05

NSERVERS = 10
NPERF50K = 0.0
NPERF1M = 0.0
NPERF5M = 0.0

# TODO make this work for any month
DESCRIPTOR_MONTH = 11
DESCRIPTOR_YEAR = 2012

DOCHURN=False

class Relay():
    def __init__(self, ip, bw, isExit=False):
        self.ip = ip
        self.bwconsensus = int(bw) # in bytes, from consensus
        self.isExit = isExit
        self.code = None
        
        self.bwrate = 0 # in bytes
        self.bwburst = 0 # in bytes
        self.bwtstamp = 0
        
        self.maxobserved = 0 # in bytes
        self.maxread = 0 # in bytes
        self.maxwrite = 0 # in bytes
        
        self.upload = 0 # in KiB
        self.download = 0 # in KiB

        self.rates = [] # list of bytes/s histories

    def getBWRateArg(self): # the min required Tor config
        return 30720 if self.bwrate < 30720 else self.bwrate

    def getBWBurstArg(self):
        return 30720 if self.bwburst < 30720 else self.bwburst

    def getBWConsensusArg(self):
        return 1000 if self.bwconsensus <= 0 else self.bwconsensus
        
    def setRegionCode(self, code):
        self.code = code
        
    def setLimits(self, bwrate, bwburst, bwtstamp):
        # defaults are 5MiB rate (5120000), 10MiB Burst (10240000)
        # minimums are 30KiB rate (30720)
        if bwtstamp > self.bwtstamp:
            self.bwtstamp = bwtstamp
            self.bwrate = int(bwrate)
            self.bwburst = int(bwburst)
        
    # max observed from server descriptor (min of max-read and max-write over 10 second intervals)
    def setMaxObserved(self, observed):
        self.maxobserved = max(self.maxobserved, observed)
        
    # max read and write over 15 minute intervals from extra-infos
    def setMaxSpeeds(self, read, write):
        self.maxread = max(self.maxread, read)
        self.maxwrite = max(self.maxwrite, write)
        
    def computeSpeeds(self):
        '''
        compute the link speeds to the ISP
        
        we prefer relay measurements over the consensus, because the measurement
        is generally more accurate and unlikely malicious
        
        we can estimate the link speed as the maximum bandwidth we've ever seen.
        this is usually the observed bandwidth from the server descriptor since
        its computed over 10 second intervals rather than the read/write histories
        from the extra-info which are averaged over 15 minutes.
        
        since the observed bandwidth reported in the server descriptor is the minimum
        of the 10 second interval reads and writes, we use the extra-infos
        to determine which of read or write this observed bandwidth likely
        corresponds to. then we compute the missing observed value (the max of the
        10 second interval reads and writes) using the ratio of read/write from the
        extra-info.
        
        in the absence of historical data we fall back to the consensus bandwidth 
        and hope that TorFlow accurately measured in this case
        '''
        if self.maxobserved > 0:
            if self.maxread > 0 and self.maxwrite > 0:
                # yay, best case as we have all the data
                readToWriteRatio = float(self.maxread)/float(self.maxwrite)
                bw = int(self.maxobserved / 1024.0)
                if readToWriteRatio > 1.0:
                    # write is the min and therefore the 'observed' value
                    self.upload = bw # the observed min
                    self.download = int(bw*readToWriteRatio) # the scaled up max
                else:
                    # read is the min and therefore the 'observed' value
                    self.download = bw # the observed min
                    self.upload = int(bw*(1.0/readToWriteRatio)) # the scaled up max
            else:
                # ok, at least use our observed
                bw = int(self.maxobserved / 1024.0)
                self.download = bw
                self.upload = bw                
        elif self.maxread > 0 and self.maxwrite > 0:
            # use read/write directly
            self.download = int(self.maxread / 1024.0)
            self.upload = int(self.maxwrite / 1024.0)               
        else:
            # pity...
            bw = int(self.bwconsensus / 1024.0)
            self.download = bw
            self.upload = bw
            
        # the 'tiered' approach, not currently used
        '''
        if self.ispbandwidth <= 512: self.ispbandwidth = 512
        elif self.ispbandwidth <= 1024: self.ispbandwidth = 1024 # 1 MiB
        elif self.ispbandwidth <= 10240: self.ispbandwidth = 10240 # 10 MiB
        elif self.ispbandwidth <= 25600: self.ispbandwidth = 25600 # 25 MiB
        elif self.ispbandwidth <= 51200: self.ispbandwidth = 51200 # 50 MiB
        elif self.ispbandwidth <= 76800: self.ispbandwidth = 76800 # 75 MiB
        elif self.ispbandwidth <= 102400: self.ispbandwidth = 102400 # 100 MiB
        elif self.ispbandwidth <= 153600: self.ispbandwidth = 153600 # 150 MiB
        else: self.ispbandwidth = 204800
        '''
        
    CSVHEADER = "IP,CCode,IsExit,Consensus(KB/s),Rate(KiB/s),Burst(KiB/s),MaxObserved(KiB/s),MaxRead(KiB/s),MaxWrite(KiB/s),LinkDown(KiB/s),LinkUp(KiB/s),Load(KiB/s)"

    def toCSV(self):
        c = str(int(self.bwconsensus/1000.0)) # should be KB, just like in consensus
        r = str(int(self.bwrate/1024.0))
        b = str(int(self.bwburst/1024.0))
        mo = str(int(self.maxobserved/1024.0))
        mr = str(int(self.maxread/1024.0))
        mw = str(int(self.maxwrite/1024.0))
        ldown = str(int(self.download))
        lup = str(int(self.upload))
        load = str(0)
        if len(self.rates) > 0: load = str(int(mean(self.rates)/1024.0))
        return ",".join([self.ip, self.code, str(self.isExit), c, r, b, mo, mr, mw, ldown, lup, load])

class GeoIPEntry():
    def __init__(self, lownum, highnum, countrycode):
        self.lownum = int(lownum)
        self.highnum = int(highnum)
        self.countrycode = countrycode
    
def main():
    ap = argparse.ArgumentParser(description='Generate hosts.xml file for Scallion Tor experiments in Shadow', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        
    # configuration options
    ap.add_argument('-p', '--prefix', action="store", dest="prefix", help="PATH to base Shadow installation", metavar="PATH", default=INSTALLPREFIX)
    ap.add_argument('--nrelays', action="store", type=int, dest="nrelays", help="number N of total relays for the generated topology", metavar='N', default=NRELAYS)
    ap.add_argument('--fexit', action="store", type=float, dest="exitfrac", help="fraction F of relays that are exits", metavar='F', default=FEXIT)
    ap.add_argument('--nclients', action="store", type=int, dest="nclients", help="number N of total clients for the generated topology", metavar='N', default=NCLIENTS)
    ap.add_argument('--fim', action="store", type=float, dest="fim", help="fraction F of interactive client connections", metavar='F', default=FIM)
    ap.add_argument('--fweb', action="store", type=float, dest="fweb", help="fraction F of web client connections", metavar='F', default=FWEB)
    ap.add_argument('--fbulk', action="store", type=float, dest="fbulk", help="fraction F of bulk HTTP client connections", metavar='F', default=FBULK)
    ap.add_argument('--fp2p', action="store", type=float, dest="fp2p", help="fraction F of bulk P2P clients", metavar='F', default=FP2P)
    ap.add_argument('--nperf50k', action="store", type=float, dest="nperf50k", help="number N of 50KiB perf clients", metavar='F', default=NPERF50K)
    ap.add_argument('--nperf1m', action="store", type=float, dest="nperf1m", help="number N of 1MiB perf clients", metavar='F', default=NPERF1M)
    ap.add_argument('--nperf5m', action="store", type=float, dest="nperf5m", help="number N of 5MiB perf clients", metavar='F', default=NPERF5M)
    ap.add_argument('--nservers', action="store", type=int, dest="nservers", help="number N of fileservers", metavar='N', default=NSERVERS)
    ap.add_argument('--dochurn', action="store_true", dest="dochurn", help="use random file selection for clients", default=DOCHURN)

    # positional args (required)
    ap.add_argument('alexa', action="store", type=str, help="path to an ALEXA file (produced with contrib/parsealexa.py)", metavar='ALEXA', default=None)
    ap.add_argument('consensus', action="store", type=str, help="path to a current Tor CONSENSUS file", metavar='CONSENSUS', default=None)
    ap.add_argument('descriptors', action="store", type=str, help="path to top-level directory containing current Tor server-descriptors", metavar='DESCRIPTORS', default=None)
    ap.add_argument('extrainfos', action="store", type=str, help="path to top-level directory containing current Tor extra-infos", metavar='EXTRAINFOS', default=None)
    ap.add_argument('connectingusers', action="store", type=str, help="path to csv containing Tor directly connecting user country data", metavar='CONNECTINGUSERS', default=None)
     
    # get arguments, accessible with args.value
    args = ap.parse_args()
    
    totalclientf = args.fim + args.fweb + args.fbulk + args.fp2p
    if totalclientf != 1.0:
        log("client fractions do not add to 1.0! please fix arguments...")
        return
    
    # fixup paths from user
    args.prefix = os.path.abspath(os.path.expanduser(args.prefix))
    args.consensus = os.path.abspath(os.path.expanduser(args.consensus))
    args.alexa = os.path.abspath(os.path.expanduser(args.alexa))
    args.descriptors = os.path.abspath(os.path.expanduser(args.descriptors))
    args.extrainfos = os.path.abspath(os.path.expanduser(args.extrainfos))
    args.connectingusers = os.path.abspath(os.path.expanduser(args.connectingusers))
    
    # we'll need to convert IPs to cluster codes
    args.geoippath = os.path.abspath(args.prefix+"/share/geoip")
    
    generate(args)
    log("finished generating:\n{0}/relays.csv\n{0}/hosts.xml\n{0}/im.dl\n{0}/web.dl\n{0}/bulk.dl\n{0}/webthink.dat\n{0}/imthink.dat".format(os.getcwd()))

def generate(args):
    # get list of relays, sorted by increasing bandwidth
    relays = parse_consensus(args.consensus)
    
    # separate out exits and nonexits
    exits, nonexits = [], []
    for relay in relays:
        if relay.isExit: exits.append(relay)
        else: nonexits.append(relay)
        
    geoentries = getGeoEntries(args.geoippath)

    # sample for the exits and nonexits we'll use for our nodes
    nexits = int(args.exitfrac * args.nrelays)
    exitnodes = getRelays(exits, nexits, geoentries, args.descriptors, args.extrainfos)
    nnonexits = args.nrelays - nexits
    nonexitnodes = getRelays(nonexits, nnonexits, geoentries, args.descriptors, args.extrainfos)
    
    servers = getServers(geoentries, args.alexa)
    clientCountryCodes = getClientCountryChoices(args.connectingusers)
    
    # output choices
    with open("relays.csv", "wb") as f:
        print >>f, Relay.CSVHEADER
        for r in exitnodes:
            print >>f, r.toCSV()
        for r in nonexitnodes:
            print >>f, r.toCSV()
    
    # build the XML
    root = etree.Element("hosts")
    
    # servers
    fim = open("im.dl", "wb")
    fweb = open("web.dl", "wb")
    fbulk = open("bulk.dl", "wb")
    fall = open("all.dl", "wb")
    fperf50k = open("50kib.dl", "wb")
    fperf1m = open("1mib.dl", "wb")
    fperf5m = open("5mib.dl", "wb")
    
    # for all.dl, ratio of bulk to web files needs to be about 1/10 of the
    # ratio of bulk to web users given as input, so the number of total BT
    # connections throughout the sim stays at the given ratio
    webPerBulk = int(1.0 / (args.fbulk / 10.0))
    
    i = 0
    while i < args.nservers:
        serverip, servercode = chooseServer(servers)
        i += 1
        name = "server{0}".format(i)
        e = etree.SubElement(root, "node")
        e.set("id", name)
        e.set("ip", serverip)
        e.set("cluster", servercode)
        e.set("bandwidthup", "102400") # in KiB
        e.set("bandwidthdown", "102400") # in KiB
        e.set("quantity", "1")
        e.set("cpufrequency", "10000000") # 10 GHz b/c we dont want bottlenecks
        a = etree.SubElement(e, "application")
        a.set("plugin", "filetransfer")
        a.set("starttime", "1")
        a.set("arguments", "server 80 {0}share".format(INSTALLPREFIX))
        print >>fim, "{0}:80:/1KiB.urnd".format(name)
        print >>fweb, "{0}:80:/320KiB.urnd".format(name)
        print >>fbulk, "{0}:80:/5MiB.urnd".format(name)
        print >>fall, "{0}:80:/5MiB.urnd".format(name)
        print >>fperf50k, "{0}:80:/50KiB.urnd".format(name)
        print >>fperf1m, "{0}:80:/1MiB.urnd".format(name)
        print >>fperf5m, "{0}:80:/5MiB.urnd".format(name)
        for j in xrange(webPerBulk): print >>fall, "{0}:80:/320KiB.urnd".format(name)
    fim.close()
    fweb.close()
    fbulk.close()
    fall.close()
    fperf50k.close()
    fperf1m.close()
    fperf5m.close()
    
    # torrent auth
    if args.fp2p > 0.0:
        e = etree.SubElement(root, "node")
        e.set("id", "auth.torrent")
        e.set("bandwidthup", "102400") # in KiB
        e.set("bandwidthdown", "102400") # in KiB
        e.set("quantity", "1")
        e.set("cpufrequency", "10000000") # 10 GHz b/c we dont want bottlenecks
        a = etree.SubElement(e, "application")
        a.set("plugin", "torrent")
        a.set("starttime", "1")
        a.set("arguments", "authority 5000")
    
    # think time file for web clients
    maxthink = 60000.0 # milliseconds
    step = 500
    entries = range(1, int(maxthink)+1, step)
    increment = 1.0 / len(entries)
    # 1012.000 0.0062491534
    with open("webthink.dat", "wb") as fthink:
        frac = increment
        for ms in entries:
            assert frac <= 1.0
            print >>fthink, "{0} {1}".format("%.3f" % ms, "%.10f" % frac)
            frac += increment
            
    # think time file for im clients
    maxthink = 5000.0 # milliseconds
    step = 500
    entries = range(1, int(maxthink)+1, step)
    increment = 1.0 / len(entries)
    with open("imthink.dat", "wb") as fthink:
        entries = range(1, int(maxthink)+1, step)
        frac = increment
        for ms in entries:
            assert frac <= 1.0
            print >>fthink, "{0} {1}".format("%.3f" % ms, "%.10f" % frac)
            frac += increment

    # think time file for perf clients
    ms = 60000.0 # milliseconds
    with open("perfthink.dat", "wb") as fthink:
        print >>fthink, "{0} {1}".format("%.3f" % ms, "%.10f" % 1.0)
    
    # authority - choose the fastest relay (no authority is an exit node)
    authority = nonexitnodes.pop(-1)
    name = "4uthority"
    starttime = "2"
    torargs = "dirauth {0} {1} {2} ./authority.torrc ./data/authoritydata {3}share/geoip".format(authority.getBWConsensusArg(), authority.getBWRateArg(), authority.getBWBurstArg(), INSTALLPREFIX) # in bytes
    addRelayToXML(root, starttime, torargs, None, None, name, authority.download, authority.upload, authority.ip, authority.code)
    
    # boot relays equally spread out between 1 and 11 minutes
    secondsPerRelay = 600.0 / (len(exitnodes) + len(nonexitnodes))
    relayStartTime = 60.0 # minute 1
    
    # exit relays
    i = 1
    for exit in exitnodes:
        assert exit.isExit is True
        
        name = "exit{0}".format(i)
        starttime = "{0}".format(int(round(relayStartTime)))
        torargs = "exitrelay {0} {1} {2} ./exit.torrc ./data/exitdata {3}share/geoip".format(exit.getBWConsensusArg(), exit.getBWRateArg(), exit.getBWBurstArg(), INSTALLPREFIX) # in bytes
        
        addRelayToXML(root, starttime, torargs, None, None, name, exit.download, exit.upload, exit.ip, exit.code)
        
        relayStartTime += secondsPerRelay
        i += 1
    
    # regular relays
    i = 1
    for relay in nonexitnodes:
        assert relay.isExit is not True
        
        name = "nonexit{0}".format(i)
        starttime = "{0}".format(int(round(relayStartTime)))
        torargs = "relay {0} {1} {2} ./relay.torrc ./data/relaydata {3}share/geoip".format(relay.getBWConsensusArg(), relay.getBWRateArg(), relay.getBWBurstArg(), INSTALLPREFIX) # in bytes
        
        addRelayToXML(root, starttime, torargs, None, None, name, relay.download, relay.upload, relay.ip, relay.code)
    
        relayStartTime += secondsPerRelay
        i += 1

    # clients
    nimclients = int(args.fim * args.nclients)
    nbulkclients = int(args.fbulk * args.nclients)
    np2pclients = int(args.fp2p * args.nclients)
    nwebclients = int(args.nclients - nimclients - nbulkclients - np2pclients)
    nperf50kclients = int(args.nperf50k)
    nperf1mclients = int(args.nperf1m)
    nperf5mclients = int(args.nperf5m)
        
    # boot clients equally spread out between 15 and 25 minutes
    secondsPerClient = 600.0 / (nimclients+nbulkclients+np2pclients+nwebclients+nperf50kclients+nperf1mclients+nperf5mclients)
    clientStartTime = 900.0 # minute 15

    if args.dochurn: # user chooses bulk/web download randomly
        i = 1
        while i <= args.nclients:
            name = "client{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./all.dl localhost 9000 ./webthink.dat -1"
            
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1       
        
    else: # user are separated into bulk/web downloaders who always download their file type
        i = 1
        while i <= nimclients:
            name = "imclient{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./im.dl localhost 9000 ./imthink.dat -1"
            
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1
                
        i = 1
        while i <= nwebclients:
            name = "webclient{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./web.dl localhost 9000 ./webthink.dat -1"
            
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1
        
        i = 1
        while i <= nbulkclients:
            name = "bulkclient{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./bulk.dl localhost 9000 none -1"
            
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1
            
        i = 1
        while i <= np2pclients:
            name = "p2pclient{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            torrentargs = "torrent node auth.torrent 5000 localhost 9000 6000 700MB"
 
            addRelayToXML(root, starttime, torargs, None, torrentargs, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1

        i = 1
        while i <= nperf50kclients:
            name = "perfclient50k{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./torperf.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./50kib.dl localhost 9000 ./perfthink.dat -1"
 
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1

        i = 1
        while i <= nperf1mclients:
            name = "perfclient1m{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./torperf.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./1mib.dl localhost 9000 ./perfthink.dat -1"
 
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1

        i = 1
        while i <= nperf5mclients:
            name = "perfclient5m{0}".format(i)
            starttime = "{0}".format(int(round(clientStartTime)))
            torargs = "client {0} {1} {2} ./torperf.torrc ./data/clientdata {3}share/geoip".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
            fileargs = "client multi ./5mib.dl localhost 9000 ./perfthink.dat -1"
 
            addRelayToXML(root, starttime, torargs, fileargs, None, name, code=choice(clientCountryCodes))
        
            clientStartTime += secondsPerClient
            i += 1
                   
    # finally, print the XML file
    with open("hosts.xml", 'wb') as fhosts:
        # plug-ins
        e = etree.Element("plugin")
        e.set("id", "scallion")
        e.set("path", "{0}plugins/libshadow-plugin-scallion.so".format(INSTALLPREFIX))
        root.insert(0, e)
        
        e = etree.Element("plugin")
        e.set("id", "filetransfer")
        e.set("path", "{0}plugins/libshadow-plugin-filetransfer.so".format(INSTALLPREFIX))
        root.insert(0, e)
        
        e = etree.Element("plugin")
        e.set("id", "torrent")
        e.set("path", "{0}plugins/libshadow-plugin-torrent.so".format(INSTALLPREFIX))
        root.insert(0, e)
        
        # kill time
        e = etree.Element("kill")
        e.set("time", "3600")
        root.insert(0, e)
        
        # all our hosts
        print >>fhosts, (etree.tostring(root, pretty_print=True, xml_declaration=False))

def addRelayToXML(root, starttime, torargs, fileargs, torrentargs, name, download=0, upload=0, ip=None, code=None): # bandwidth in KiB
    # node
    e = etree.SubElement(root, "node")
    e.set("id", name)
    if ip is not None: e.set("ip", ip)
    if code is not None: e.set("cluster", code)
    
    # bandwidth is optional in XML, will be assigned based on cluster if not given
    if download > 0: e.set("bandwidthdown", "{0}".format(download)) # in KiB
    if upload > 0: e.set("bandwidthup", "{0}".format(upload)) # in KiB
    
    e.set("quantity", "1")
    e.set("cpufrequency", choice(CPUFREQS))

    # applications - wait 5 minutes to start applications
    if torargs is not None:
        a = etree.SubElement(e, "application")
        a.set("plugin", "scallion")
        a.set("starttime", "{0}".format(int(starttime)))
        a.set("arguments", torargs)
    if fileargs is not None:
        a = etree.SubElement(e, "application")
        a.set("plugin", "filetransfer")
        a.set("starttime", "{0}".format(int(starttime)+300))
        a.set("arguments", fileargs)
    if torrentargs is not None:
        a = etree.SubElement(e, "application")
        a.set("plugin", "torrent")
        a.set("starttime", "{0}".format(int(starttime)+300))
        a.set("arguments", torrentargs)

def getClientCountryChoices(connectinguserspath):
    lines = None
    with open(connectinguserspath, 'rb') as f:
        lines = f.readlines()
    
    assert len(lines) > 11
    header = lines[0].strip().split(',')
    
    total = 0
    counts = dict()
    for linei in xrange(len(lines)-10, len(lines)):
        line = lines[linei]
        parts = line.strip().split(',')
        for i in xrange(2, len(parts)-1): # exclude the last total column "all"
            if parts[i] != "NA" and parts[i] != "all":
                country = header[i]
                n = int(parts[i])
                total += n
                if country not in counts: counts[country] = 0
                counts[country] += n
                
    codes = []
    for c in counts:
        frac = float(counts[c]) / float(total)
        n = int(frac * 1000)
        
        code = c.upper()
#        if code == "US" or code == "A1" or code == "A2": code = "USMN"
        code = "{0}{0}".format(code)
        
        for i in xrange(n):
            codes.append(code)
    
    return codes

def ip2long(ip):
    """
    Convert a IPv4 address into a 32-bit integer.
    
    @param ip: quad-dotted IPv4 address
    @type ip: str
    @return: network byte order 32-bit integer
    @rtype: int
    """
    ip_array = ip.split('.')
    ip_long = long(ip_array[0]) * 16777216 + long(ip_array[1]) * 65536 + long(ip_array[2]) * 256 + long(ip_array[3])
    return ip_long
    
def getClusterCode(geoentries, ip):
    # use geoip entries to find our cluster code for IP
    ipnum = ip2long(ip)
    #print "{0} {1}".format(ip, ipnum)
    # theres probably a faster way of doing this, but this is python and i dont care
    for entry in geoentries:
        if ipnum >= entry.lownum and ipnum <= entry.highnum: 
#            if entry.countrycode == "US": return "USMN" # we have no USUS code (USMN gets USCENTRAL)
            return "{0}{0}".format(entry.countrycode)
    log("Warning: Cant find code for IP '{0}' Num '{1}', defaulting to 'USUS'".format(ip, ipnum))
    return "USUS"

def getGeoEntries(geoippath):
    entries = []
    with open(geoippath, "rb") as f:
        for line in f:
            if line[0] == "#": continue
            parts = line.strip().split(',')
            entry = GeoIPEntry(parts[0], parts[1], parts[2])
            entries.append(entry)
    return entries

def getServers(geoentries, alexapath):
    # return IPs from args.alexa, keeping sort order
    servers = {}
    servers['allips'] = []
    servers['codes'] = {}
    servers['iptocode'] = {}
    
    with open(alexapath, 'rb') as f:
        for line in f:
            parts = line.strip().split(',')
            ip = parts[2]
            servers['allips'].append(ip)

            code = getClusterCode(geoentries, ip)
            servers['iptocode'][ip] = code
            
            if code not in servers['codes']:
                servers['codes'][code] = {}
                servers['codes'][code]['ips'] = []
                servers['codes'][code]['index'] = 0
            
            servers['codes'][code]['ips'].append(ip)
            
    return servers

def chooseServer(servers):
    # first get a random code
    tempip = choice(servers['allips'])
    code = servers['iptocode'][tempip]
    
    # now we have our code, get the next index in this code's list
    s = servers['codes'][code]
    i = s['index'] % (len(s['ips']))
    ip = s['ips'][i]
    s['index'] += 1
    
    return ip, code
    
def getRelays(relays, k, geoentries, descriptorpath, extrainfopath):
    sample = sample_relays(relays, k)
    
    # maps for easy relay lookup while parsing descriptors
    ipmap, fpmap = dict(), dict()
    for s in sample:
        if s.ip not in ipmap: 
            ipmap[s.ip] = s
        
    # go through all the descriptors and find the bandwidth rate, burst, and
    # history from the most recent descriptor of each relay in our sample
    for root, dirs, files in os.walk(descriptorpath):
        for filename in files: 
            fullpath = os.path.join(root, filename)
            with open(fullpath, 'rb') as f:
                rate, burst, observed = 0, 0, 0
                ip = ""
                fingerprint = None
                published = None
                
                for line in f:
                    parts = line.strip().split()
                    if len(parts) == 0: continue
                    if parts[0] == "router":
                        ip = parts[2]
                        if ip not in ipmap: break
                    elif parts[0] == "published":
                        published = "{0} {1}".format(parts[1], parts[2])
                    elif parts[0] == "bandwidth":
                        rate, burst, observed = int(parts[1]), int(parts[2]), int(parts[3])
                    elif parts[0] == "opt" and parts[1] == "fingerprint":
                        fingerprint = "".join(parts[2:])
                
                if ip not in ipmap: continue
                
                relay = ipmap[ip]
                # we want to know about every fingerprint
                if fingerprint is not None: fpmap[fingerprint] = relay
                # we want to know every observed bandwidth to est. link speed
                relay.setMaxObserved(observed)
                # we only want the latest rate and burst settings 
                if published is not None:
                    datet = datetime.strptime(published, "%Y-%m-%d %H:%M:%S")
                    unixt = time.mktime(datet.timetuple())
                    relay.setLimits(rate, burst, unixt)
                        
    # now check for extra info docs for our chosen relays, so we get read and write histories
    # here the published time doesnt matter b/c we are trying to estimate the
    # relay's ISP link speed
    for root, dirs, files in os.walk(extrainfopath):
        for filename in files: 
            fullpath = os.path.join(root, filename)
            with open(fullpath, 'rb') as f:
                maxwrite, maxread = 0, 0
                totalwrite, totalread = 0, 0
                fingerprint = None
                published = None
                
                for line in f:
                    parts = line.strip().split()
                    if len(parts) == 0: continue
                    if parts[0] == "extra-info":
                        if len(parts) < 3: break # cant continue if we dont know the relay
                        fingerprint = parts[2]
                        if fingerprint not in fpmap: break
                    elif parts[0] == "published":
                        # only count data from our modeled month towards our totals 
                        published = "{0} {1}".format(parts[1], parts[2])
                        datet = datetime.strptime(published, "%Y-%m-%d %H:%M:%S")
                        if datet.year != DESCRIPTOR_YEAR or datet.month != DESCRIPTOR_MONTH:
                            published = None
                    elif parts[0] == "write-history":
                        if len(parts) < 6: continue # see if we can get other info from this doc
                        seconds = float(int(parts[3][1:]))
                        speeds = parts[5]
                        bytes = speeds.split(',')
                        maxwrite = int(max([int(i) for i in bytes]) / seconds)
                        totalwrite = int(float(sum([int(i) for i in bytes])) / float(seconds*len(bytes)))
                    elif parts[0] == "read-history":
                        if len(parts) < 6: continue # see if we can get other info from this doc
                        seconds = float(int(parts[3][1:]))
                        speeds = parts[5]
                        bytes = speeds.split(',')
                        maxread = int(max([int(i) for i in bytes]) / seconds)
                        totalread = int(float(sum([int(i) for i in bytes])) / float(seconds*len(bytes)))
                        
                if fingerprint is not None and fingerprint in fpmap:
                    relay = fpmap[fingerprint]
                    relay.setMaxSpeeds(maxread, maxwrite)
                    if published is not None: relay.rates.append(totalread + totalwrite)
                        
    # make sure we found some info for all of them, otherwise use defaults
    for s in sample:
        s.setRegionCode(getClusterCode(geoentries, s.ip))
        s.computeSpeeds()
    
    return sample
    
def sample_relays(relays, k):
    """
    sample k of n relays
    split list into k buckts, take the median from each bucket
    this provides the statistically best fit to the original list
    
    relays list should be sorted by bandwidth
    """
    n = len(relays)
    if k >= n: 
        k = n
        print "choosing {0} of {1} relays".format(k, n)
    assert k <= n
    
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
                bw = float(line.strip().split()[1].split("=")[1]) * 1000.0 # KB to bytes
    
    return sorted(relays, key=lambda relay: relay.getBWConsensusArg())

def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] scallion: "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
