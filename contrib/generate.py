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

import os, sys, subprocess, argparse, socket, time
from random import choice
from datetime import datetime
from lxml import etree
from lxml.html.builder import INS

# This should NOT be expanded, we'll use this directly in the XML file
INSTALLPREFIX="~/.shadow/"

# distribution of CPU frequencies, in KHz
CPUFREQS=["2200000", "2400000", "2600000", "2800000", "3000000", "3200000", "3400000"]

NRELAYS = 10
FEXIT = 0.4
NCLIENTS = 100
FBULK = 0.05
NSERVERS = 10

class Relay():
    def __init__(self, ip, bw, isExit=False):
        self.ip = ip
        self.bw = int(bw) # in bytes
        self.isExit = isExit
        self.code = None
        self.bwrate = None # in bytes
        self.bwburst = None # in bytes
        self.bwhistory = None # in bytes
        
        self.ispbandwidth = 0 # in KiB
        kbw = bw / 1024
        # select correct bandwidth tier
        if kbw <= 512: self.ispbandwidth = 512
        elif kbw <= 1024: self.ispbandwidth = 1024 # 1 MiB
        elif kbw <= 10240: self.ispbandwidth = 10240 # 10 MiB
        elif kbw <= 25600: self.ispbandwidth = 25600 # 25 MiB
        elif kbw <= 51200: self.ispbandwidth = 51200 # 50 MiB
        elif kbw <= 76800: self.ispbandwidth = 76800 # 75 MiB
        elif kbw <= 102400: self.ispbandwidth = 102400 # 100 MiB
        elif kbw <= 153600: self.ispbandwidth = 153600 # 150 MiB
        else: self.ispbandwidth = 204800
        
    def setTokenBucketBW(self, bwrate, bwburst, bwhistory):
        self.bwrate = int(bwrate)
        self.bwburst = int(bwburst)
        self.bwhistory = int(bwhistory)
        
    def setRegionCode(self, code):
        self.code = code

    def toCSV(self):
        return ",".join([self.ip, self.code, str(self.isExit), str(self.bw), str(self.bwrate), str(self.bwburst), str(self.bwhistory)])

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
    ap.add_argument('--fbulk', action="store", type=float, dest="fbulk", help="fraction F of bulk downloading clients (remaining are web clients)", metavar='F', default=FBULK)
    ap.add_argument('--nservers', action="store", type=int, dest="nservers", help="number N of fileservers", metavar='N', default=NSERVERS)

    # positional args (required)
    ap.add_argument('alexa', action="store", type=str, help="path to an ALEXA file (produced with contrib/parsealexa.py)", metavar='ALEXA', default=None)
    ap.add_argument('consensus', action="store", type=str, help="path to a current Tor CONSENSUS file", metavar='CONSENSUS', default=None)
    ap.add_argument('descriptors', action="store", type=str, help="path to top-level directory containing current Tor server-descriptors", metavar='DESCRIPTORS', default=None)
    ap.add_argument('connectingusers', action="store", type=str, help="path to csv containing Tor directly connecting user country data", metavar='CONNECTINGUSERS', default=None)
    
    # get arguments, accessible with args.value
    args = ap.parse_args()
    
    # fixup paths from user
    args.prefix = os.path.abspath(os.path.expanduser(args.prefix))
    args.consensus = os.path.abspath(os.path.expanduser(args.consensus))
    args.alexa = os.path.abspath(os.path.expanduser(args.alexa))
    args.descriptors = os.path.abspath(os.path.expanduser(args.descriptors))
    args.connectingusers = os.path.abspath(os.path.expanduser(args.connectingusers))
    
    # we'll need to convert IPs to cluster codes
    args.geoippath = os.path.abspath(args.prefix+"/share/geoip")
    
    generate(args)
    log("finished generating:\n{0}/hosts.xml\n{0}/web.dl\n{0}/bulk.dl\n{0}/think.dat".format(os.getcwd()))

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
    exitnodes = getRelays(exits, nexits, geoentries, args.descriptors)
    nnonexits = args.nrelays - nexits
    nonexitnodes = getRelays(nonexits, nnonexits, geoentries, args.descriptors)
    
    servers = getServers(args.alexa)
    clientCountryCodes = getClientCountryChoices(args.connectingusers)
    
    # build the XML
    root = etree.Element("hosts")
    
    # servers
    e = etree.SubElement(root, "software")
    e.set("id", "filesoft")
    e.set("plugin", "filetransfer")
    e.set("time", "1")
    e.set("arguments", "server 80 {0}share".format(INSTALLPREFIX))

    fweb = open("web.dl", "wb")
    fbulk = open("bulk.dl", "wb")
    i = 0
    while i < args.nservers:
        serverip = servers[i%len(servers)]
        servercode = getClusterCode(geoentries, serverip)
        i += 1
        name = "server{0}".format(i)
        e = etree.SubElement(root, "node")
        e.set("id", name)
        e.set("ip", serverip)
        e.set("cluster", servercode)
        e.set("software", "filesoft")
        e.set("bandwidthup", "102400") # in KiB
        e.set("bandwidthdown", "102400") # in KiB
        e.set("quantity", "1")
        e.set("cpufrequency", choice(CPUFREQS))
        print >>fweb, "{0}:80:/320KiB.urnd".format(name)
        print >>fbulk, "{0}:80:/5MiB.urnd".format(name)
    fweb.close()
    fbulk.close()
    
    # think time file for web clients
    maxthink = 20000.0 # milliseconds
    increment = 1.0 / maxthink
    # 1012.000 0.0062491534
    with open("think.dat", "wb") as fthink:
        frac = increment
        for ms in xrange(1, int(maxthink)+1):
            assert frac <= 1.0
            print >>fthink, "{0} {1}".format("%.3f" % ms, "%.10f" % frac)
            frac += increment
    
    # authority - choose the fastest relay (no authority is an exit node)
    authority = nonexitnodes.pop(-1)
    name = "4uthority"
    soft = "{0}soft".format(name)
    starttime = "2"
    softargs = "dirauth {0} {1} {2} ./authority.torrc ./data/authoritydata {3}share/geoip".format(authority.bw, authority.bwrate, authority.bwburst, INSTALLPREFIX) # in bytes
    addRelayToXML(root, soft, starttime, softargs, name, authority.ispbandwidth, authority.ip, authority.code)
    
    # nod boot-up rates
    relaysPerSecond = 5
    clientsPerSecond = 10
    
    # exit relays
    i = 1
    timecounter = 60 # start creating exit nodes at 60 seconds
    for exit in exitnodes:
        assert exit.isExit is True
        
        name = "exit{0}".format(i)
        soft = "{0}soft".format(name)
        starttime = "{0}".format(timecounter)
        softargs = "exitrelay {0} {1} {2} ./exit.torrc ./data/exitdata {3}share/geoip".format(exit.bw, exit.bwrate, exit.bwburst, INSTALLPREFIX) # in bytes
        
        addRelayToXML(root, soft, starttime, softargs, name, exit.ispbandwidth, exit.ip, exit.code)
        
        if i % relaysPerSecond == 0: timecounter += 1 # x nodes every second
        i += 1
    
    timecounter += 1
    i = 1
        
    # regular relays
    for relay in nonexitnodes:
        assert relay.isExit is not True
        
        name = "nonexit{0}".format(i)
        soft = "{0}soft".format(name)
        starttime = "{0}".format(timecounter)
        softargs = "relay {0} {1} {2} ./relay.torrc ./data/relaydata {3}share/geoip".format(relay.bw, relay.bwrate, relay.bwburst, INSTALLPREFIX) # in bytes
        
        addRelayToXML(root, soft, starttime, softargs, name, relay.ispbandwidth, relay.ip, relay.code)
    
        if i % relaysPerSecond == 0: timecounter += 1 # x nodes every second
        i += 1
        
    # earliest client start time
    timecounter = 900

    # shadowperf
    name = "shadowperfclient"
    soft = "{0}soft".format(name)
    starttime = "{0}".format(timecounter)
    softargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip client double server1 80 localhost 9000 {3}share/50KiB.urnd {3}share/1MiB.urnd none 30".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
    
    addRelayToXML(root, soft, starttime, softargs, name, code=choice(clientCountryCodes))
        
    # clients
    nbulkclients = int(args.fbulk * args.nclients)
    nwebclients = args.nclients - nbulkclients

    i = 1
    while i < nwebclients:
        name = "webclient{0}".format(i)
        soft = "{0}soft".format(name)
        starttime = "{0}".format(timecounter)
        softargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip client multi ./web.dl localhost 9000 ./think.dat -1".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
        
        addRelayToXML(root, soft, starttime, softargs, name, code=choice(clientCountryCodes))
    
        if i % clientsPerSecond == 0: timecounter += 1 # x nodes every second
        i += 1
    
    i = 1
    timecounter += 1
    while i < nbulkclients:
        name = "bulkclient{0}".format(i)
        soft = "{0}soft".format(name)
        starttime = "{0}".format(timecounter)
        softargs = "client {0} {1} {2} ./client.torrc ./data/clientdata {3}share/geoip client multi ./bulk.dl localhost 9000 none -1".format(10240000, 5120000, 10240000, INSTALLPREFIX) # in bytes
        
        addRelayToXML(root, soft, starttime, softargs, name, code=choice(clientCountryCodes))
    
        if i % clientsPerSecond == 0: timecounter += 1 # x nodes every second
        i += 1
        
    # finally, print the XML file
    with open("hosts.xml", 'wb') as fhosts:
        # plug-ins
        e = etree.Element("plugin")
        e.set("id", "scallion")
        e.set("path", "{0}plugins/libshadow-plugin-scallion.so".format(INSTALLPREFIX))
        print >>fhosts, (etree.tostring(e, pretty_print=True, xml_declaration=True))
        
        e = etree.Element("plugin")
        e.set("id", "filetransfer")
        e.set("path", "{0}plugins/libshadow-plugin-filetransfer.so".format(INSTALLPREFIX))
        print >>fhosts, (etree.tostring(e, pretty_print=True, xml_declaration=False))
        
        # kill time
        e = etree.Element("kill")
        e.set("time", "3600")
        print >>fhosts, (etree.tostring(e, pretty_print=True, xml_declaration=False))
        
        # all our hosts
        print >>fhosts, (etree.tostring(root, pretty_print=True, xml_declaration=False))

def addRelayToXML(root, soft, starttime, softargs, name, ispbandwidth=0, ip=None, code=None): # bandwidth in KiB
    # software
    e = etree.SubElement(root, "software")
    e.set("id", soft)
    e.set("plugin", "scallion")
    e.set("time", starttime)
    e.set("arguments", softargs)
    
    # node
    e = etree.SubElement(root, "node")
    e.set("id", name)
    if ip is not None: e.set("ip", ip)
    if code is not None: e.set("cluster", code)
    e.set("software", soft)
    if ispbandwidth > 0: # bandwidth is optional in XML, will be assigned based on cluster if not given
        e.set("bandwidthup", "{0}".format(ispbandwidth)) # in KiB
        e.set("bandwidthdown", "{0}".format(ispbandwidth)) # in KiB
    e.set("quantity", "1")
    e.set("cpufrequency", choice(CPUFREQS))

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

def getServers(alexapath):
    # return IPs from args.alexa, keeping sort order
    ips = []
    with open(alexapath, 'rb') as f:
        for line in f:
            parts = line.strip().split(',')
            ip = parts[2]
            ips.append(ip)
    return ips

def getRelays(relays, k, geoentries, descriptorpath):
    sample = sample_relays(relays, k)
    
    # get a map for easy lookup while parsing descriptors
    ipmap = dict()
    timemap = dict()
    for s in sample:
        if s.ip not in ipmap: 
            ipmap[s.ip] = s
            timemap[s.ip] = 0.0
        
    # go through all the descriptors and find the bandwidth rate, burst, and
    # history from the most recent descriptor of each relay in our sample
    for root, dirs, files in os.walk(descriptorpath):
        for filename in files: 
            fullpath = os.path.join(root, filename)
            with open(fullpath, 'rb') as f:
                rate, burst, hist = 0, 0, 0
                ip = ""
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
                        rate, burst, hist = int(parts[1]), int(parts[2]), int(parts[3])
                        
                if published is not None and ip in ipmap:
                    datet = datetime.strptime(published, "%Y-%m-%d %H:%M:%S")
                    unixt = time.mktime(datet.timetuple())
                    if unixt >= timemap[ip]:
                        timemap[ip] = unixt
                        relay = ipmap[ip]
                        relay.setTokenBucketBW(rate, burst, hist)
    
    # make sure we found some info for all of them, otherwise use defaults
    for s in sample:
        if s.bwrate <= 0 or s.bwburst <= 0: s.setTokenBucketBW(5120000, 10240000, 0) # 5MB rate, 10MB Burst
        s.setRegionCode(getClusterCode(geoentries, s.ip))
    
    return sample
    
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
                bw = float(line.strip().split()[1].split("=")[1]) * 1024.0 # KiB to bytes
    
    return sorted(relays, key=lambda relay: relay.bw)

def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] scallion: "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
