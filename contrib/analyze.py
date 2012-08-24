#! /usr/bin/python

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

"""
analyze.py

Utility to help analyze results from the Shadow simulator. Use
'$ python analyze --help' to get started
"""

import sys, os, argparse, subprocess, pylab, numpy

##==========================================================================
## <config>
"""
Modify this to include results from several experiments into each figure
generated with the plot subcommand. Compressed results extracted with the parse 
subcommand for each experiment should be in a separate directory. Specify those
directories here, and some other plot options.

Example:
experiments = [
("trial1", "b", "-", "./trial1/"),
("trial2", "r", "--", "./trial2/"),
]

Format:
A list of four-tuples for each trial, where:
-index 0 is the pylab label for the legend
-index 1 is the pylab color
-index 2 is the pylab linestyle
-index 3 is the output directory from the parse subcommand, containing the
parsed gzipped results
"""
experiments = [
    ("results", "k", "-", "./results"),
#    ("tiny", "g", "-", "./tiny"),
#    ("small", "b", "-", "./small"),
#    ("medium", "r", "-", "./medium"),
#    ("large", "k", "-", "./large"),
]
## </config>
##==========================================================================


## default path to shadow log file
LOGPATH=os.path.abspath(os.path.expanduser("./shadow.log"))
## default path to store parsed results
OUTPUTPATH=os.path.abspath(os.path.expanduser("./results"))
## ignore parsed downloads after a default of 30 minutes
CUTOFF=1800.0 

## default directory to store the single graphs we generate
GRAPHPATH=os.path.abspath(os.path.expanduser("./graphs"))
## the default prefix for the filenames of those graphs
PREFIX="results-"
## include this title in each graph
TITLE="Shadow Results"

## make font sizes larger
#pylab.rcParams.update({'font.size': 16})

## move around the viewing scope
pylab.rcParams.update({'figure.subplot.left': 0.14})
pylab.rcParams.update({'figure.subplot.right': 0.96})
pylab.rcParams.update({'figure.subplot.bottom': 0.15})
pylab.rcParams.update({'figure.subplot.top': 0.87})

def main():
    parser_main = argparse.ArgumentParser(
        description='Utility to help analyze results from the Shadow simulator', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # setup our commands
    subparsers_main = parser_main.add_subparsers(
        help='parse or plot Shadow results (for help use <subcommand> --help)')
    
    # configure parse subcommand
    parser_parse = subparsers_main.add_parser('parse',
        description="""Parse a Shadow logfile to extract meaningful performance 
                and load data from an experiment. The data is stored in the 
                specified output directory in a compressed format that can later 
                be graphed with the plot subcommand.""", 
        help="extract meaningful performance and load data from an experiment",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_parse.set_defaults(func=parse, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # add parsing options
    parser_parse.add_argument('-l', '--log', 
        help="PATH to a Shadow log file", 
        metavar="PATH",
        action="store", dest="logpath",
        default=LOGPATH)

    parser_parse.add_argument('-o', '--output', 
        help="""PATH to a directory where we should store extracted results. 
                The directory will be created if it does not exist.""", 
        metavar="PATH",
        action="store", dest="outputpath",
        default=OUTPUTPATH)

    parser_parse.add_argument('-c', '--cutoff', 
        help="ignore all downloads that completed before S seconds", 
        metavar="S",
        action="store", dest="cutoff",
        default=CUTOFF)

    # configure plot subcommand
    parser_plot = subparsers_main.add_parser('plot',
        description="""Plot the compressed data extracted with the parse 
                subcommand to produce figures that graphically represent 
                the experimental results. 

                If you want to use this script to plot results, YOU MUST EDIT
                THE SCRIPT WITH THE OUTPUT PATH TO YOUR PARSED RESULTS!

                The output from each run of parse should be placed in separate 
                directories, and each of those directories should be specified
                in the "experiments" list at the top of this script. This way 
                the script knows what results to plot, and we can combine 
                results from multiple experiments into the same set of graphs.
                (This may eventually move to a config file instead.)""",
        help="produce figures that graphically represent the experimental results",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_plot.set_defaults(func=plot, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    
    # add plotting options
    parser_plot.add_argument('-t', '--title', 
        help="a STRING title added to graphs we generate", 
        metavar="STRING",
        action="store", dest="title",
        default=TITLE)

    parser_plot.add_argument('-p', '--prefix', 
        help="a STRING filename prefix for graphs we generate", 
        metavar="STRING",
        action="store", dest="prefix",
        default=PREFIX)

    parser_plot.add_argument('-o', '--output', 
        help="PATH to store each individual graph we generate", 
        metavar="PATH",
        action="store", dest="graphpath",
        default=GRAPHPATH)

    # get arguments, accessible with args.value
    args = parser_main.parse_args()

    # run chosen command
    args.func(args)

def parse(args):
    logpath = os.path.abspath(os.path.expanduser(args.logpath))
    outputpath = os.path.abspath(os.path.expanduser(args.outputpath))
    cutoff = int(args.cutoff)

    if not os.path.exists(outputpath): os.makedirs(outputpath)

    # will store the perf data we extract, keyed by client type
    ttfb = {'web': [], 'bulk': [], 'im': [], 'p2p': [], 'perf50k' : [], 'perf1m' : [], 'perf5m' : []}
    ttlb = {'web': [], 'bulk': [], 'im': [], 'p2p': [], 'perf50k' : [], 'perf1m' : [], 'perf5m' : []}

    # will store the throughput data (total bytes over time)
    tput = {'read' : {}, 'write' : {}}

    # simulation timing
    times = {'real' : [], 'virtual': []}

    # parse the log file
    print "Parsing '{0}'".format(logpath)
    with open(logpath, 'r') as f:
        lastt = 0

        for line in f:
            parts = line.strip().split()
            if parts[0].find(':') < 0: continue

            nodename = parts[4]
            
            # timing
            realts, virtualts = parts[0], parts[2]
            if realts != "n/a" and virtualts != "n/a":
                realt = parsetimestamp(realts)
                virtualt = parsetimestamp(virtualts)
                
                # only save timestamps once per virtual second
                if virtualt > lastt:
                    times['real'].append(realt/3600.0)
                    times['virtual'].append(virtualt/3600.0)
                    lastt = virtualt

                # dont look at perf before the cuttoff
                if virtualt < cutoff: continue

                # perf data
                if parts[6] == "[fg-download-complete]":
                    fbtime = float(parts[11])
                    bytes = float(parts[16])
                    lbtime = float(parts[19])
                    
                    if nodename.find("imclient") > -1:
                        ttfb['im'].append(fbtime)
                        ttlb['im'].append(lbtime)
                    elif nodename.find("webclient") > -1:
                        ttfb['web'].append(fbtime)
                        ttlb['web'].append(lbtime)
                    elif nodename.find("bulkclient") > -1:
                        ttfb['bulk'].append(fbtime)
                        ttlb['bulk'].append(lbtime)
                    elif nodename.find("perfclient50k") > -1:
                        ttfb['perf50k'].append(fbtime)
                        ttlb['perf50k'].append(lbtime)
                    elif nodename.find("perfclient1m") > -1:
                        ttfb['perf1m'].append(fbtime)
                        ttlb['perf1m'].append(lbtime)
                    elif nodename.find("perfclient5m") > -1:
                        ttfb['perf5m'].append(fbtime)
                        ttlb['perf5m'].append(lbtime)
#                   else: print nodename, bytes; assert 0

                elif parts[6]== "[client-block-complete]":
                    fbtime = float(parts[10])
                    bytes = float(parts[19])
                    lbtime = float(parts[22])
                    ttfb['p2p'].append(fbtime)
                    ttlb['p2p'].append(lbtime)

                # throughput data
                if parts[5] == "[tracker_heartbeat]":
                    cpu_percent = float(parts[8])
                    mem_total_kib = float(parts[11])
                    seconds = float(int(parts[14]))
                    allocated_kib = float(parts[17])
                    deallocated_kib = float(parts[20])
                    received_bytes = float(parts[23])
                    sent_bytes = float(parts[26])

                    # MiB/s
                    r_mibps = received_bytes / (seconds*1024.0*1024.0)
                    w_mibps = sent_bytes / (seconds*1024.0*1024.0)

                    i = int(float(virtualt) / seconds)
                    if i not in tput['read']: tput['read'][i] = 0.0
                    if i not in tput['write']: tput['write'][i] = 0.0
                    tput['read'][i] += (r_mibps)
                    tput['write'][i] += (w_mibps)

    # get the data into plottable format
    for k in ttfb: ttfb[k].sort()
    for k in ttlb: ttlb[k].sort()

    # save our extracted data

    print "Saving extracted results to '{0}'".format(outputpath)

    save(outputpath, "ttfb-im.gz", ttfb['im'])
    save(outputpath, "ttfb-web.gz", ttfb['web'])
    save(outputpath, "ttfb-bulk.gz", ttfb['bulk'])
    save(outputpath, "ttfb-p2p.gz", ttfb['p2p'])
    save(outputpath, "ttfb-perf50k.gz", ttfb['perf50k'])
    save(outputpath, "ttfb-perf1m.gz", ttfb['perf1m'])
    save(outputpath, "ttfb-perf5m.gz", ttfb['perf5m'])

    save(outputpath, "ttlb-im.gz", ttlb['im'])
    save(outputpath, "ttlb-web.gz", ttlb['web'])
    save(outputpath, "ttlb-bulk.gz", ttlb['bulk'])
    save(outputpath, "ttlb-p2p.gz", ttlb['p2p'])
    save(outputpath, "ttlb-perf50k.gz", ttlb['perf50k'])
    save(outputpath, "ttlb-perf1m.gz", ttlb['perf1m'])
    save(outputpath, "ttlb-perf5m.gz", ttlb['perf5m'])

    save(outputpath, "time-virtual.gz", times['virtual'])
    save(outputpath, "time-real.gz", times['real'])

    # [d[x] for x in sorted(d.keys())] # sort dict d by keys
    save(outputpath, "tput-read.gz", tput['read'].values())
    save(outputpath, "tput-write.gz", tput['write'].values())

    # aggregate stats

    print "Some quick stats:"

    countmsg = "\tdownload counts: {0} web, {1} bulk".format(len(ttlb['web']), len(ttlb['bulk']))
    tputreadmsg = "\ttput read MiB/s: {0} min, {1} max, {2} median, {3} mean".format(min(tput['read'].values()), max(tput['read'].values()), numpy.median(tput['read'].values()), numpy.mean(tput['read'].values()))
    tputwritemsg = "\ttput write MiB/s: {0} min, {1} max, {2} median, {3} mean".format(min(tput['write'].values()), max(tput['write'].values()), numpy.median(tput['write'].values()), numpy.mean(tput['write'].values()))
    timemsg = "\ttiming hours: {0} virtual in {1} real".format(times['virtual'][-1], times['real'][-1])

    with open("{0}/parse-stats".format(outputpath), 'wb') as f:
        print >>f, countmsg
        print countmsg
        print >>f, tputreadmsg
        print tputreadmsg
        print >>f, tputwritemsg
        print tputwritemsg
        print >>f, timemsg
        print timemsg

    print "Done!"

def plot(args):
    graphpath = os.path.abspath(os.path.expanduser(args.graphpath))
    prefix = args.prefix
    title = args.title

    ## load the data archives
    data = {}
    for e in experiments:
        data[e[0]] = {
            'ttfb-im': load("{0}/ttfb-im.gz".format(e[3])), 
            'ttlb-im': load("{0}/ttlb-im.gz".format(e[3])), 
            'ttfb-web': load("{0}/ttfb-web.gz".format(e[3])), 
            'ttlb-web': load("{0}/ttlb-web.gz".format(e[3])), 
            'ttfb-bulk': load("{0}/ttfb-bulk.gz".format(e[3])), 
            'ttlb-bulk': load("{0}/ttlb-bulk.gz".format(e[3])), 
            'ttfb-p2p': load("{0}/ttfb-p2p.gz".format(e[3])), 
            'ttlb-p2p': load("{0}/ttlb-p2p.gz".format(e[3])), 
            'ttfb-perf50k': load("{0}/ttfb-perf50k.gz".format(e[3])), 
            'ttlb-perf50k': load("{0}/ttlb-perf50k.gz".format(e[3])), 
            'ttfb-perf1m': load("{0}/ttfb-perf1m.gz".format(e[3])), 
            'ttlb-perf1m': load("{0}/ttlb-perf1m.gz".format(e[3])), 
            'ttfb-perf5m': load("{0}/ttfb-perf5m.gz".format(e[3])), 
            'ttlb-perf5m': load("{0}/ttlb-perf5m.gz".format(e[3])), 
            'time-virtual' : load("{0}/time-virtual.gz".format(e[3])),
            'time-real' : load("{0}/time-real.gz".format(e[3])),
            'tput-read' : load("{0}/tput-read.gz".format(e[3])),
            'tput-write' : load("{0}/tput-write.gz".format(e[3])),
        }

    if not os.path.exists(graphpath): os.mkdir(graphpath)
    savedfigures = []

    ######################
    # time to first byte #
    ######################

    print "Generating 'time to first byte' graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-im']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-im']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nIM Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-im.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-web']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-web']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nWeb Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-web.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-bulk']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-bulk']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nBulk Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-bulk.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-p2p']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-p2p']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nP2P Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-p2p.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-perf50k']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-perf50k']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nShadowPerf 50KiB Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-perf50k.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-perf1m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-perf1m']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nShadowPerf 1 MiB Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-perf1m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-perf5m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttfb-perf5m']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nShadowPerf 5MiB Clients".format(title))
        pylab.xlabel("Time to First Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttfb-perf5m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # time to last byte  #
    ######################

    print "Generating 'time to last byte' graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-im']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-im']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nIM Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-im.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-web']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-web']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nWeb Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=35.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-web.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-bulk']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-bulk']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nBulk Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=150.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-bulk.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-p2p']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-p2p']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nP2P Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=25.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-p2p.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-perf50k']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-perf50k']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nShadowPerf 50KiB Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-perf50k.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-perf1m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-perf1m']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nShadowPerf 1 MiB Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=50.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-perf1m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-perf5m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x, y = getcdf(data[e[0]]['ttlb-perf5m']); pylab.plot(x, y, label="{0}".format(e[0]), c=e[1], ls=e[2])
        pylab.title("{0}\nShadowPerf 5MiB Clients".format(title))
        pylab.xlabel("Time to Last Byte (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=165.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}ttlb-perf5m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # run timings        #
    ######################

    print "Generating timing graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['time-real']) > 0 and len(data[e[0]]['time-virtual']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: x = data[e[0]]['time-real']; y = data[e[0]]['time-virtual']; pylab.plot(x, y, label=e[0], c=e[1], ls=e[2])
        pylab.title("{0}\nExperiment Timing".format(title))
        pylab.xlabel("Real Time (h)")
        pylab.ylabel("Virtual Time (h)")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}timing.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # load               #
    ######################

    print "Generating load/throughput graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tput-read']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: y = data[e[0]]['tput-read']; x = xrange(len(y)); pylab.plot(x, y, label=e[0], c=e[1], ls=e[2])
        pylab.title("Network Data Read Over Time")
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Read (MiB/s)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}tput-read.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tput-write']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        for e in experiments: y = data[e[0]]['tput-write']; x = xrange(len(y)); pylab.plot(x, y, label=e[0], c=e[1], ls=e[2])
        pylab.title("Network Data Written Over Time")
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Writen (MiB/s)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}tput-write.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    for e in experiments:
        web_num = len(data[e[0]]['ttlb-web'])
        bulk_num = len(data[e[0]]['ttlb-bulk'])
        im_num = len(data[e[0]]['ttlb-im'])
        p2p_num = len(data[e[0]]['ttlb-p2p'])
        perf50k_num = len(data[e[0]]['ttlb-perf50k'])
        perf1m_num = len(data[e[0]]['ttlb-perf1m'])
        perf5m_num = len(data[e[0]]['ttlb-perf5m'])

        total_num = web_num + bulk_num + im_num + p2p_num + perf50k_num + perf1m_num + perf5m_num

        web_gib = web_num * 320.0 / (1024.0*1024.0)
        bulk_gib = bulk_num * 5120.0 / (1024.0*1024.0)
        im_gib = im_num * 1.0 / (1024.0*1024.0)
        p2p_gib = p2p_num * 16.0 / (1024.0*1024.0)
        perf50k_gib = perf50k_num * 50.0 / (1024.0*1024.0)
        perf1m_gib = perf1m_num * 1024.0 / (1024.0*1024.0)
        perf5m_gib = perf5m_num * 5120.0 / (1024.0*1024.0)

        total_gib = web_gib + bulk_gib + im_gib + p2p_gib + perf50k_gib + perf1m_gib + perf5m_gib

        pylab.figure()
        pylab.axes([0.2, 0.05, 0.6, 0.8])

        labels, fractions = [], []
        if web_num > 0: 
            fractions.append(web_gib/total_gib)
            labels.append("Web\n{1}%\n{0}GiB".format(decimals(web_gib), decimals(100.0*fractions[-1])))
        if bulk_num > 0: 
            fractions.append(bulk_gib/total_gib)
            labels.append("Bulk\n{1}%\n{0}GiB".format(decimals(bulk_gib), decimals(100.0*fractions[-1])))
        if im_num > 0: 
            fractions.append(im_gib/total_gib)
            labels.append("IM\n{1}%\n{0}GiB".format(decimals(im_gib), decimals(100.0*fractions[-1])))
        if p2p_num > 0: 
            fractions.append(p2p_gib/total_gib)
            labels.append("P2P\n{1}%\n{0}GiB".format(decimals(p2p_gib), decimals(100.0*fractions[-1])))
        if perf50k_num > 0: 
            fractions.append(perf50k_gib/total_gib)
            labels.append("Perf50K\n{1}%\n{0}GiB".format(decimals(perf50k_gib), decimals(100.0*fractions[-1])))
        if perf1m_num > 0: 
            fractions.append(perf1m_gib/total_gib)
            labels.append("Perf1M\n{1}%\n{0}GiB".format(decimals(perf1m_gib), decimals(100.0*fractions[-1])))
        if perf5m_num > 0: 
            fractions.append(perf5m_gib/total_gib)
            labels.append("Perf5M\n{1}%\n{0}GiB".format(decimals(perf5m_gib), decimals(100.0*fractions[-1])))

        # show fractions of pie with autopct='%1.2f%%'
        patches = pylab.pie(fractions, explode=[0.1]*len(fractions), autopct=None, shadow=True)

        leg = pylab.legend(patches, labels, loc=(-0.3, -0.05), labelspacing=0.8)
        pylab.setp(leg.get_texts(), fontsize='6')

        pylab.title("{0}\nTotal Client Load, {1}: {2} GiB".format(title, e[0], decimals(total_gib)))
        figname = "{0}/{1}{2}-client-load.pdf".format(graphpath, prefix, e[0])
        savedfigures.append(figname)
        pylab.savefig(figname)

    ## concat the subgraphs into a single .pdf
    if which('pdftk') is not None:
        print "Concatenating the graphs from {0} using the 'pdftk' program".format(graphpath)
        cmd = list(savedfigures)
        cmd.insert(0, "pdftk")
        name = "{0}-combined.pdf".format(prefix[:len(prefix)-1])
        cmd.extend(["cat", "output", name])
        subprocess.call(cmd)
        print "Successfully created '{0}'!".format(name)
    print "Done!"

# helper - save files
def save(outputpath, filename, data):
    if len(data) > 0: numpy.savetxt("{0}/{1}".format(outputpath, filename), data)

## helper - load the data in the correct format for plotting
def load(path):
    p = os.path.abspath(path)
    if not os.path.exists(p): return []
    print "Loading data from '{0}'".format(p)
    data = numpy.loadtxt(path)
    return numpy.atleast_1d(data).tolist()

# helper - parse shadow timestamps
def parsetimestamp(stamp):
    parts = stamp.split(":")
    h, m, s = int(parts[0]), int(parts[1]), int(parts[2])
    seconds = (h*3600) + (m*60) + (s)
    return seconds

## helper - cumulative fraction for y axis
def cf(d): return pylab.arange(1.0,float(len(d))+1.0)/float(len(d))

## helper - return step-based CDF x and y values
def getcdf(data):
    data.sort()
    frac = cf(data)
    x, y, lasty = [], [], 0.0
    for i in xrange(len(data)):
        x.append(data[i])
        y.append(lasty)
        x.append(data[i])
        y.append(frac[i])
        lasty = frac[i]
    return x, y

## helper - round or extend the number of decimals to 3
def decimals(v): return "%.2f" % v

## helper - test if program is in path
def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None

if __name__ == '__main__':
    main()

