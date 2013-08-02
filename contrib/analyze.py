#! /usr/bin/python

"""
analyze.py

Utility to help analyze results from the Shadow simulator. Use
'$ python analyze --help' to get started
"""

import sys, os, argparse, subprocess, pylab, numpy, itertools, gzip, cPickle

## PARSING DEFAULTS

## default path to shadow log file
LOGPATH=os.path.abspath(os.path.expanduser("./shadow.log"))
## default path to store parsed results
OUTPUTPATH=os.path.abspath(os.path.expanduser("./results"))
## ignore parsed downloads after a default of 30 minutes
CUTOFF=1800.0 

## PLOTTING DEFAULTS

## default directory to store the single graphs we generate
GRAPHPATH=os.path.abspath(os.path.expanduser("./graphs"))
## the default prefix for the filenames of those graphs
PREFIX="results"
## default supertitle in each graph
SUPERTITLE=None
SUPERTITLE_SIZE="small"
TITLE_SIZE="x-small"
## use this line format cycle
LINEFORMATS="k-,r-,b-,g-,c-,m-,y-,k--,r--,b--,g--,c--,m--,y--,k:,r:,b:,g:,c:,m:,y:,k-.,r-.,b-.,g-.,c-., m-.,y-."

pylab.rcParams.update({
    'backend': 'PDF',
#    'font.size': 16,
    'figure.figsize': (4,3),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.14,
    'figure.subplot.right': 0.96,
    'figure.subplot.bottom': 0.15,
    'figure.subplot.top': 0.87,
    'grid.color': '0.1',
    'axes.grid' : True,
    'axes.titlesize' : 'small',
    'axes.labelsize' : 'small',
    'axes.formatter.limits': (-3,3),
    'xtick.labelsize' : 'small',
    'ytick.labelsize' : 'small',
    'lines.linewidth' : 2.0,
    'lines.markeredgewidth' : 0.5,
    'lines.markersize' : 5,
    'legend.fontsize' : 'x-small',
    'legend.fancybox' : False,
    'legend.shadow' : False,
    'legend.ncol' : 1.0,
    'legend.borderaxespad' : 0.5,
    'legend.numpoints' : 1,
    'legend.handletextpad' : 0.5,
    'legend.handlelength' : 2.25,
    'legend.labelspacing' : 0.25,
    'legend.markerscale' : 1.0,
#    'ps.useafm' : True,
#    'pdf.use14corefonts' : True,
#    'text.usetex' : True,
})

# a custom action for passing in experimental data directories when plotting
class PlotDataAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        # extract the path to our data, and the label for the legend
        datapath = os.path.abspath(os.path.expanduser(values[0]))
        label = values[1]
        # check the path exists
        if not os.path.exists(datapath): raise argparse.ArgumentError(self, "The supplied path to the plot data does not exist: '{0}'".format(datapath))
        # remove the default
        if "_didremovedefault" not in namespace:
            setattr(namespace, self.dest, [])
            setattr(namespace, "_didremovedefault", True)
        # append out new experiment path
        dest = getattr(namespace, self.dest)
        dest.append([datapath, label])

# parent class for all node statistics
class NodeStats():
    def __init__(self, name):
        self.name = name
        self.rtput = {} # read throughput over time
        self.wtput = {} # write throughput over time
        self.cpu = {} # cpu usage over time
        self.mem = {} # running total mem usage over time
        self.alloc = {} # mem allocated at time
        self.dealloc = {} # mem deallocated at time

        self.circuits = {} # circuits by id
        self.circttb = {} # time to build over time
        self.circttf = {} # time to failure over time
        self.circrt = {} # runtime - time circuits were open over time
        self.circhops = {} # chosen circuit hops over time

        self.streams = {} # streams by id
        self.strmttb = {} # time to build (establish) over time
        self.strmttf = {} # time to failure over time
        self.strmrt = {} # time streams were open over time

        self.sktbufs = {} # socket buffer length/size
        self.socketpeers = {}

    def parse(self, parts):
        virtualt = parsetimestamp(parts[2])
        tick = int(round(virtualt)) # second

        # shadow heartbeat node stats
        if 'shadow-heartbeat' in parts[6] and parts[7] == "[node]":
            nodeparts = parts[8].split(',')

            seconds = int(nodeparts[0])
            received = int(nodeparts[1]) # in bytes
            sent = int(nodeparts[2]) # in bytes
            cpu = float(nodeparts[3]) # in percentage (utilization)
            n_events_delayed = int(nodeparts[4])
            avg_delay = float(nodeparts[5]) # milliseconds

            # MiB/s
            r_mibps = received / (seconds*1024.0*1024.0)
            w_mibps = sent / (seconds*1024.0*1024.0)

            # redefine tick
            tick = int(float(virtualt) / seconds)
            if tick not in self.rtput: self.rtput[tick] = 0.0
            self.rtput[tick] += (r_mibps)
            if tick not in self.wtput: self.wtput[tick] = 0.0
            self.wtput[tick] += (w_mibps)
            if tick not in self.cpu: self.cpu[tick] = 0.0
            self.cpu[tick] += (cpu)

        elif 'shadow-heartbeat' in parts[6] and parts[7] == "[ram]":
            ramparts = parts[8].split(',')

            seconds = int(ramparts[0])
            allocated = int(ramparts[1]) # in bytes
            deallocated = int(ramparts[2]) # in bytes
            total = int(ramparts[3]) # in bytes
            n_pointers_tracked = int(ramparts[4])
            n_failed_frees = int(ramparts[5])

            alloc_mib = allocated / (1024.0*1024.0)
            dealloc_mib = deallocated / (1024.0*1024.0)
            total_mib = total / (1024.0*1024.0)

            tick = int(float(virtualt) / float(seconds))
            if tick not in self.mem: self.mem[tick] = 0.0
            self.mem[tick] += total_mib
            if tick not in self.alloc: self.alloc[tick] = 0.0
            self.alloc[tick] += alloc_mib
            if tick not in self.dealloc: self.dealloc[tick] = 0.0
            self.dealloc[tick] += dealloc_mib

        elif 'shadow-heartbeat' in parts[6] and parts[7] == "[socket]":
            sockets = parts[8].strip(';').split(';')

            total_input_length = 0
            total_input_size = 0
            total_output_length = 0
            total_output_size = 0
            for socket_buffer in sockets:
                info = socket_buffer.split(',')
                descriptor = '{0} - {1}'.format(info[0], info[2])
                input_buffer_length = int(info[3])
                input_buffer_size = int(info[4])
                output_buffer_length = int(info[5])
                output_buffer_size = int(info[6])
                
                if '127.0.0.1' in descriptor:
                    continue
                
                total_input_length += input_buffer_length
                total_input_size += input_buffer_size
                total_output_length += output_buffer_length
                total_output_size += output_buffer_size
                
                if descriptor not in self.sktbufs: self.sktbufs[descriptor] = []
                self.sktbufs[descriptor].append([tick, input_buffer_length, input_buffer_size, output_buffer_length, output_buffer_size])
            
            
            if 'total' not in self.sktbufs: self.sktbufs['total'] = []
            self.sktbufs['total'].append([tick, total_input_length, total_input_size, total_output_length, total_output_size])    
            
        # tor controller stats
        elif parts[6] == "[torcontrol-log]":
            event = parts[8]

            if event == "ORCONN": pass

            elif event == "CIRC":
                # NOTE: this can be tricky, b/c circuits can be canibalized and reused
                # meaning they may have multiple built/closed/failed events
                id = int(parts[9])
                status = parts[10]

                if status == "LAUNCHED": pass

                elif status == "EXTENDED": pass

                elif status == "BUILT":
                    timecreated = parsecreatetime(parts[10:])
                    timebuilt = virtualt
                    buildtime = timebuilt-timecreated
                    if buildtime < 0: return # WTF!?

                    path = []
                    longpath = parts[11] if len(parts)>11 else None
                    if longpath is not None:
                        for longname in longpath.split(','):
                            name = longname.split('~')[1] if '~' in longname else longname
                            path.append(name)                    

                    if id not in self.circuits: self.circuits[id] = Circuit(id, timecreated, timebuilt, path)

                    if tick not in self.circttb: self.circttb[tick] = []
                    self.circttb[tick].append(buildtime)
                    if tick not in self.circhops: self.circhops[tick] = []
                    for r in path: self.circhops[tick].append(r)

                elif status == "CLOSED":
                    if id in self.circuits and self.circuits[id].timeclosed is not None:
                        self.circuits[id].timeclosed = virtualt
                        self.circuits[id].reasonclosed = parsecode(parts[10:], "REASON")
                        runtime = self.circuits[id].timeclosed - self.circuits[id].timecreated
                        if runtime < 0: return # WTF!?

                        if tick not in self.circrt: self.circrt[tick] = []
                        self.circrt[tick].append(runtime)

                elif status == "FAILED":
                    timecreated = parsecreatetime(parts[10:])
                    timefailed = virtualt
                    failtime = timefailed-timecreated
                    if failtime < 0: return # WTF!?
                    reasonfailed = parsecode(parts[10:], "REASON")

                    if tick not in self.circttf: self.circttf[tick] = []
                    self.circttf[tick].append(failtime)

            elif event == "CIRC_MINOR": pass

            elif event == "STREAM":
                id = int(parts[9])
                status = parts[10]
                circid = int(parts[11])
                target = parts[12]

                if status == "NEW": pass

                elif status == "NEWRESOLVE": pass

                elif status == "REMAP": pass

                elif status == "SENTCONNECT":
                    if id not in self.streams: self.streams[id] = Stream(id, virtualt, circid, target)

                elif status == "SENTRESOLVE": pass

                elif status == "SUCCEEDED":
                    if id in self.streams and self.streams[id].timeestablished is None:
                        self.streams[id].timeestablished = virtualt
                        buildtime = self.streams[id].timeestablished - self.streams[id].timecreated
                        if buildtime < 0: return # WTF!?

                        if tick not in self.strmttb: self.strmttb[tick] = []
                        self.strmttb[tick].append(buildtime)

                elif status == "FAILED":
                    if id in self.streams and self.streams[id].timeclosed is None: 
                        self.streams[id].timeclosed = -1
                        self.streams[id].timeestablished = -1
                        failtime = virtualt - self.streams[id].timecreated
                        if failtime < 0: return # WTF!?

                        if tick not in self.strmttf: self.strmttf[tick] = []
                        self.strmttf[tick].append(failtime)

                elif status == "CLOSED":
                    if id in self.streams and self.streams[id].timeclosed is None: 
                        self.streams[id].timeclosed = virtualt
                        runtime = self.streams[id].timeclosed - self.streams[id].timeestablished
                        if runtime < 0: return # WTF!?

                        if tick not in self.strmrt: self.strmrt[tick] = []
                        self.strmrt[tick].append(runtime)

                elif status == "DETACHED": pass

            elif event == "BW":
                read = int(parts[9])
                write = int(parts[10])

            elif event == "STREAM_BW": pass

            elif event == "EXIT_BW": pass

            elif event == "DIR_BW": pass

            elif event == "OR_BW": pass

            elif event == "GUARD": pass

            elif event == "BUILDTIMEOUT_SET": pass

        return tick

# holds relay statistics
class RelayStats(NodeStats):
    def __init__(self, name):
        NodeStats.__init__(self, name)
        self.celln = {} # total number of cell stats events reported over time
        self.cellpqp = {} # total cells processed appward over time
        self.cellpqw = {} # total cell queue waiting time appward over time
        self.cellpql = {} # ave cell queue length appward over time
        self.cellnqp = {} # total cells processed exitward over time
        self.cellnqw = {} # total cell queue waiting time exitward over time
        self.cellnql = {} # ave cell queue length exitward over time

        self.tokengr = {} # global read bucket ave percentage filled over time
        self.tokengw = {} # global write bucket ave percentage filled over time
        self.tokenrr = {} # relay read bucket ave percentage filled over time
        self.tokenrw = {} # relay write bucket ave percentage filled over time
        self.tokenor = {} # orconn read bucket ave percentage filled over time
        self.tokenow = {} # orconn write bucket ave percentage filled over time

    def parse(self, parts):
        tick = NodeStats.parse(self, parts)
        virtualt = parsetimestamp(parts[2])

        # tor controller stats
        if parts[6] == "[torcontrol-log]":
            event = parts[8]

            if event == "CLIENTS_SEEN": pass

            elif event == "CELL_STATS":
                circid = int(parts[9]) # cell stats for this circuit
                pcircid = int(parts[10]) # id sent to us from the prev (appward) hop
                pproc = int(parts[11]) # total cells processed appward
                pqwait = int(parts[12]) # total queue waiting time appward
                pqlen = float(parts[13]) # ave queue length appward
                ncircid = int(parts[14]) # id sent to us from the next (exitward) hop
                nproc = int(parts[15]) # total cells processed exitward
                nqwait = int(parts[16]) # total queue waiting time exitward
                nqlen = float(parts[17]) # ave queue length exitward

                if tick not in self.celln: self.celln[tick] = 0.0
                self.celln[tick] += 1

                if tick not in self.cellpqp: self.cellpqp[tick] = 0.0
                self.cellpqp[tick] += pproc
                if tick not in self.cellpqw: self.cellpqw[tick] = 0.0
                self.cellpqw[tick] += pqwait
                if tick not in self.cellpql: self.cellpql[tick] = 0.0
                self.cellpql[tick] += pqlen
                if tick not in self.cellnqp: self.cellnqp[tick] = 0.0
                self.cellnqp[tick] += nproc
                if tick not in self.cellnqw: self.cellnqw[tick] = 0.0
                self.cellnqw[tick] += nqwait
                if tick not in self.cellnql: self.cellnql[tick] = 0.0
                self.cellnql[tick] += nqlen

            elif event == "TOKENS":
                pglobalr = int(parts[9]) # previous value of global read bucket
                globalr = int(parts[10]) # new value of global read bucket
                pglobalw = int(parts[11]) # previous value of global write bucket
                globalw = int(parts[12]) # new value of global write bucket
                prelayr = int(parts[13]) # previous value of relay read bucket
                relayr = int(parts[14]) # new value of relay read bucket
                prelayw = int(parts[15]) # previous value of relay write bucket
                relayw = int(parts[16]) # new value of relay write bucket

                # use 'previous' values to get tokens left BEFORE the refill
                if tick not in self.tokengr: self.tokengr[tick] = []
                self.tokengr[tick].append(pglobalr)
                if tick not in self.tokengw: self.tokengw[tick] = []
                self.tokengw[tick].append(pglobalw)
                if tick not in self.tokenrr: self.tokenrr[tick] = []
                self.tokenrr[tick].append(prelayr)
                if tick not in self.tokenrw: self.tokenrw[tick] = []
                self.tokenrw[tick].append(prelayw)

            elif event == "OR_TOKENS": # NOTE: tor normally doesnt have ids for orconns
                porr = int(parts[9]) # previous value of orconn read bucket
                orr = int(parts[10]) # new value of orconn read bucket
                porw = int(parts[11]) # previous value of orconn write bucket
                orw = int(parts[12]) # new value of orconn write bucket

                # use 'previous' values to get tokens left BEFORE the refill
                if tick not in self.tokenor: self.tokenor[tick] = []
                self.tokenor[tick].append(porr)
                if tick not in self.tokenow: self.tokenow[tick] = []
                self.tokenow[tick].append(porw)

# holds client statistics
class ClientStats(NodeStats):
    def __init__(self, name):
        NodeStats.__init__(self, name)
        self.downloads = []

    def parse(self, parts):
        tick = NodeStats.parse(self, parts)
        virtualt = parsetimestamp(parts[2])
        
        # filetransfer plug-in stats
        if parts[6] == "[fg-download-complete]":
            fbtime = float(parts[11])
            bytes = float(parts[16])
            lbtime = float(parts[19])
            self.downloads.append(Download(bytes, fbtime, lbtime, virtualt))

        elif parts[6] == "[client-block-complete]":
            fbtime = float(parts[10])
            bytes = float(parts[19])
            lbtime = float(parts[22])
            self.downloads.append(Download(bytes, fbtime, lbtime, virtualt))

class Download():
    def __init__(self, bytes, ttfb, ttlb, time):
        self.bytes = bytes # size of the download
        self.time = time # virtual time instant that download finished
        self.ttfb = ttfb # time to first byte of download
        self.ttlb = ttlb # time to last byte of download

class Connection():
    def __init__(self, id, time):
        pass

class Circuit():
    def __init__(self, id, timecreated, timebuilt, path):
        self.id = id # the circuit id
        self.timecreated = timecreated # the time the circuit was launched
        self.timebuilt = timebuilt # the time the circuit was successfully built
        self.path = [r for r in path] # a list of relay hostnames in the path
        self.timeclosed = None # the time the circuit closed after being built
        self.reasonclosed = None # a string, the reason from Tor

class Stream():
    def __init__(self, id, timecreated, circid, target):
        self.id = id
        self.circid = circid
        self.target = target
        self.timecreated = timecreated
        self.timeestablished = None
        self.timeclosed = None

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
    
    # requried positional arg
    parser_parse.add_argument('logpath', 
        help="path to a Shadow log file", 
        metavar="LOGFILE",
        action="store",
        default=LOGPATH)

    # add parsing options
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

    parser_parse.add_argument('-a', '--all', 
        help="export detailed stats for all nodes in addition to aggregate stats", 
        action="store_true", dest="all",
        default=False)

    # configure plot subcommand
    parser_plot = subparsers_main.add_parser('plot',
        description="""Plot the compressed data extracted with the parse 
                subcommand to produce figures that graphically represent 
                the experimental results. 

                The output from each run of parse should be placed in separate 
                directories, and each of those directories should be specified
                with '-d' flags to the 'plot' subcommand (along with labels for 
                the graph legends). This is done in order to combine results 
                from multiple experiments into the same set of graphs.""",
        help="produce figures that graphically represent the experimental results",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser_plot.set_defaults(func=plot, 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    # required option so we know what we are plotting
    parser_plot.add_argument('-d', '--data', 
        help="""Append a PATH to the directory containing the compressed data 
                that was output with the parse subcommand, and the LABEL for 
                the graph legend for a set of experimental results""", 
        metavar=("PATH", "LABEL"),
        nargs=2,
        required="True",
        action=PlotDataAction, dest="experiments")
    
    # add plotting options
    parser_plot.add_argument('-t', '--title', 
        help="a STRING title added to graphs we generate", 
        metavar="STRING",
        action="store", dest="title",
        default=SUPERTITLE)

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

    parser_plot.add_argument('-f', '--format',
        help="""A comma-separated LIST of color/line format strings to cycle to 
                matplotlib's plot command (see matplotlib.pyplot.plot)""", 
        metavar="LIST",
        action="store", dest="format",
        default=LINEFORMATS)

    # get arguments, accessible with args.value
    args = parser_main.parse_args()

    # run chosen command
    args.func(args)

def parse(args):
    logpath = os.path.abspath(os.path.expanduser(args.logpath))
    outputpath = os.path.abspath(os.path.expanduser(args.outputpath))
    cutoff = int(args.cutoff)

    if not os.path.exists(outputpath): os.makedirs(outputpath)

    # simulation timing
    times = {'real' : [], 'virtual': []}

    # statistics by nodename
    clients, relays = {}, {}

    # parse the log file
    print "Parsing '{0}'".format(logpath)
    with open(logpath, 'r') as f:
        lastt = 0

        for line in f:
            parts = line.strip().split()
            if len(parts) < 1 or parts[0].find(':') < 0: continue

            nodename = parts[4]

            # timing
            realts, virtualts = parts[0], parts[2]
            if realts != "n/a" and virtualts != "n/a":
                realt = parsetimestamp(realts)
                virtualt = parsetimestamp(virtualts)

                # only save timestamps once per virtual second
                if virtualt >= (lastt + 1):
                    times['real'].append(realt/3600.0)
                    times['virtual'].append(virtualt/3600.0)
                    lastt = virtualt

                # now parse the node-specific stats
                if nodename == "[n/a]": continue
                name = nodename[1:nodename.index("-")]
                if 'client' in name:
                    if name not in clients: clients[name] = ClientStats(name)
                    clients[name].parse(parts)
                elif 'exit' in name or 'relay' in name or 'uthority' in name:
                    if name not in relays: relays[name] = RelayStats(name)
                    relays[name].parse(parts)

    # aggregate and save results
    print "Saving results to '{0}'".format(outputpath)

    # will store the perf data we extract, keyed by client type
    ttfb = {'web': [], 'bulk': [], 'im': [], 'p2p': [], 'perf50k' : [], 'perf1m' : [], 'perf5m' : [], 'other' : []}
    ttlb = {'web': [], 'bulk': [], 'im': [], 'p2p': [], 'perf50k' : [], 'perf1m' : [], 'perf5m' : [], 'other' : []}

    for name in clients:
        c = clients[name]
        first, last = None, None
        if c.name.find("imclient") > -1: first, last = ttfb['im'], ttlb['im']
        elif c.name.find("webclient") > -1: first, last = ttfb['web'], ttlb['web']
        elif c.name.find("bulkclient") > -1: first, last = ttfb['bulk'], ttlb['bulk']
        elif c.name.find("p2pclient") > -1: first, last = ttfb['p2p'], ttlb['p2p']
        elif c.name.find("perfclient50k") > -1: first, last = ttfb['perf50k'], ttlb['perf50k']
        elif c.name.find("perfclient1m") > -1: first, last = ttfb['perf1m'], ttlb['perf1m']
        elif c.name.find("perfclient5m") > -1: first, last = ttfb['perf5m'], ttlb['perf5m']
        else: first, last = ttfb['other'], ttlb['other']
#   else: print nodename, bytes; assert 0

        myttfb, myttlb = [], []
        for dl in c.downloads: 
            # dont look at perf before the cutoff
            if dl.time < cutoff: continue
            first.append(dl.ttfb)
            if args.all: myttfb.append(dl.ttfb)
            last.append(dl.ttlb)
            if args.all: myttlb.append(dl.ttlb)
        if args.all:
            # fixup
            myttfb.sort()
            myttlb.sort()
            # save
            save(outputpath, "node/{0}/ttfb.gz".format(name), myttfb)
            save(outputpath, "node/{0}/ttlb.gz".format(name), myttlb)

    # get the data into plottable format
    for k in ttfb: ttfb[k].sort()
    for k in ttlb: ttlb[k].sort()

    # will store the throughput and memory data (total bytes over time)
    tput = {'read' : {}, 'write' : {}}
    mem = {'alloc' : {}, 'dealloc' : {}, 'total' : {}}
    # circuit builds and failures over time, timings, and relay usage
    circs = {'build' : {}, 'fail' : {}, 'buildtime' : [], 'failtime' : [], 'runtime' : [], 'hops' : {}}
    # stream builds and failures over time, and timings
    strms = {'build' : {}, 'fail' : {}, 'buildtime' : [], 'failtime' : [], 'runtime' : []}
    # cell circuit queue stats over time
    celltime = {'pqproc' : {}, 'nqproc' : {}, 'pqwait' : {}, 'nqwait' : {}, 'pqlen' : {}, 'nqlen' : {}}
    cellcdf = {'pqproc' : [], 'nqproc' : [], 'pqwait' : [], 'nqwait' : [], 'pqcellwait' : [], 'nqcellwait' : [], 'pqlen' : [], 'nqlen' : []}
    # token bucket stats over time
    tokentime = {'gread' : {}, 'gwrite' : {}, 'rread' : {}, 'rwrite' : {}, 'oread' : {}, 'owrite' : {}}
    tokencdf = {'gread' : [], 'gwrite' : [], 'rread' : [], 'rwrite' : [], 'oread' : [], 'owrite' : []}

    for name in clients.keys()+relays.keys():
        n = clients[name] if name in clients else relays[name]

        myrtput, mywtput = {}, {}
        myalloc, mydealloc, mymem = {}, {}, {}
        mycibuilds, mycifails, mycihops = {}, {}, {}
        mycibuildtimes, mycifailtimes, myciruntimes = [], [], []
        mystbuilds, mystfails = {}, {}
        mystbuildtimes, mystfailtimes, mystruntimes = [], [], []

        # read throughput
        for tick in n.rtput:
            if tick not in tput['read']: tput['read'][tick] = 0.0
            tput['read'][tick] += (n.rtput[tick])
            if args.all:
                if tick not in myrtput: myrtput[tick] = 0.0
                myrtput[tick] += (n.rtput[tick])

        # write throughput
        for tick in n.wtput:
            if tick not in tput['write']: tput['write'][tick] = 0.0
            tput['write'][tick] += (n.wtput[tick])
            if args.all:
                if tick not in mywtput: mywtput[tick] = 0.0
                mywtput[tick] += (n.wtput[tick])

        # allocated mem
        for tick in n.alloc:
            if tick not in mem['alloc']: mem['alloc'][tick] = 0.0
            mem['alloc'][tick] += (n.alloc[tick])
            if args.all:
                if tick not in myalloc: myalloc[tick] = 0.0
                myalloc[tick] += (n.alloc[tick])

        # allocated mem
        for tick in n.dealloc:
            if tick not in mem['dealloc']: mem['dealloc'][tick] = 0.0
            mem['dealloc'][tick] += (n.dealloc[tick])
            if args.all:
                if tick not in mydealloc: mydealloc[tick] = 0.0
                mydealloc[tick] += (n.dealloc[tick])

        # total mem
        for tick in n.mem:
            if tick not in mem['total']: mem['total'][tick] = 0.0
            mem['total'][tick] += (n.mem[tick])
            if args.all:
                if tick not in mymem: mymem[tick] = 0.0
                mymem[tick] += (n.mem[tick])

        # circuit builds over time, and buildtimes
        for tick in n.circttb:
            builds = n.circttb[tick]
            for t in builds: circs['buildtime'].append(t)
            if tick not in circs['build']: circs['build'][tick] = 0.0
            circs['build'][tick] += len(builds)
            if args.all:
                for t in builds: mycibuildtimes.append(t)
                if tick not in mycibuilds: mycibuilds[tick] = 0.0
                mycibuilds[tick] += len(builds)

        # circuit fails over time, and failtimes
        for tick in n.circttf:
            fails = n.circttf[tick]
            for t in fails: circs['failtime'].append(t)
            if tick not in circs['fail']: circs['fail'][tick] = 0.0
            circs['fail'][tick] += len(fails)
            if args.all:
                for t in fails: mycifailtimes.append(t)
                if tick not in mycifails: mycifails[tick] = 0.0
                mycifails[tick] += len(fails)

        # circuit runtimes
        for tick in n.circrt:
            runs = n.circrt[tick]
            for t in runs: circs['runtime'].append(t)
            if args.all:
                for t in runs: myciruntimes.append(t)

        # circuit hops
        for tick in n.circhops:
            hops = n.circhops[tick]
            for h in hops: 
                if h not in circs['hops']: circs['hops'][h] = 0.0
                circs['hops'][h] += 1
                if args.all:
                    if h not in mycihops: mycihops[h] = 0.0
                    mycihops[h] += 1

        # stream builds over time, and buildtimes
        for tick in n.strmttb:
            builds = n.strmttb[tick]
            for t in builds: strms['buildtime'].append(t)
            if tick not in strms['build']: strms['build'][tick] = 0.0
            strms['build'][tick] += len(builds)
            if args.all:
                for t in builds: mystbuildtimes.append(t)
                if tick not in mystbuilds: mystbuilds[tick] = 0.0
                mystbuilds[tick] += len(builds)

        # stream fails over time, and failtimes
        for tick in n.strmttf:
            fails = n.strmttf[tick]
            for t in fails: strms['failtime'].append(t)
            if tick not in strms['fail']: strms['fail'][tick] = 0.0
            strms['fail'][tick] += len(fails)
            if args.all:
                for t in fails: mystfailtimes.append(t)
                if tick not in mystfails: mystfails[tick] = 0.0
                mystfails[tick] += len(fails)

        # stream runtimes
        for tick in n.strmrt:
            runs = n.strmrt[tick]
            for t in runs: strms['runtime'].append(t)
            if args.all:
                for t in runs: mystruntimes.append(t)
                
        mysktbufs = n.sktbufs

        if args.all:
            # sort data for plotting
            mycibuildtimes.sort()
            mycifailtimes.sort()
            myciruntimes.sort()
            mystbuildtimes.sort()
            mystfailtimes.sort()
            mystruntimes.sort()
            # fixup any missing ticks in data-over-time collections
            mycifails = fixupticks(mycifails)
            mycibuilds = fixupticks(mycibuilds)
            mystfails = fixupticks(mystfails)
            mystbuilds = fixupticks(mystbuilds)
            # save
            save(outputpath, "node/{0}/tput-read.gz".format(name), myrtput.values())
            save(outputpath, "node/{0}/tput-write.gz".format(name), mywtput.values())
            save(outputpath, "node/{0}/mem-alloc.gz".format(name), myalloc.values())
            save(outputpath, "node/{0}/mem-dealloc.gz".format(name), mydealloc.values())
            save(outputpath, "node/{0}/mem-total.gz".format(name), mymem.values())
            save(outputpath, "node/{0}/circs-build.gz".format(name), mycibuilds.values())
            save(outputpath, "node/{0}/circs-fail.gz".format(name), mycifails.values())
            save(outputpath, "node/{0}/circs-buildtime-cdf.gz".format(name), mycibuildtimes)
            save(outputpath, "node/{0}/circs-failtime-cdf.gz".format(name), mycifailtimes)
            save(outputpath, "node/{0}/circs-runtime-cdf.gz".format(name), myciruntimes)
            save(outputpath, "node/{0}/circs-hops.gz".format(name), sorted(mycihops.values()))
            save(outputpath, "node/{0}/strms-build.gz".format(name), mystbuilds.values())
            save(outputpath, "node/{0}/strms-fail.gz".format(name), mystfails.values())
            save(outputpath, "node/{0}/strms-buildtime-cdf.gz".format(name), mystbuildtimes)
            save(outputpath, "node/{0}/strms-failtime-cdf.gz".format(name), mystfailtimes)
            save(outputpath, "node/{0}/strms-runtime-cdf.gz".format(name), mystruntimes)
            
            savepickle(outputpath, "node/{0}/socket-buffers.gz".format(name), mysktbufs)

            
    pqlencounter = 0.0
    nqlencounter = 0.0
    for name in relays.keys():
        n = relays[name]

        myctpqproc, myctnqproc, myctpqwait, myctnqwait, myctpqlen, myctnqlen = {}, {}, {}, {}, {}, {}
        myccpqproc, myccnqproc, myccpqwait, myccnqwait, myccpqcellwait, myccnqcellwait, myccpqlen, myccnqlen = [], [], [], [], [], [], [], []
        myttgread, myttgwrite, myttrread, myttrwrite, myttoread, myttowrite = {}, {}, {}, {}, {}, {}
        mytcgread, mytcgwrite, mytcrread, mytcrwrite, mytcoread, mytcowrite = [], [], [], [], [], []

        myproctotal, myproccount, mywaittotal, mywaitcount, mylentotal, mylencount = 0.0, 0.0, 0.0, 0.0, 0.0, 0.0

        # cells processed appward
        for tick in n.cellpqp: 
            proc = n.cellpqp[tick]
            if tick not in celltime['pqproc']: celltime['pqproc'][tick] = 0.0
            celltime['pqproc'][tick] += proc
            myproctotal += proc
            myproccount += 1.0
            if args.all:
                if tick not in myctpqproc: myctpqproc[tick] = 0.0
                myctpqproc[tick] += proc
                myccpqproc.append(proc)
        if myproctotal > 0: cellcdf['pqproc'].append(0.0 if myproccount==0 else myproctotal/myproccount)

        # queue time appward
        for tick in n.cellpqw:
            wait = n.cellpqw[tick]
            assert tick in n.cellpqp
            proc = n.cellpqp[tick]
            if tick not in celltime['pqwait']: celltime['pqwait'][tick] = 0.0
            celltime['pqwait'][tick] += wait
            mywaittotal += wait
            mywaitcount += 1.0
            if args.all:
                if tick not in myctpqwait: myctpqwait[tick] = 0.0
                myctpqwait[tick] += wait
                myccpqwait.append(wait)
                myccpqcellwait.append(0.0 if proc==0 else wait/proc)
        if mywaittotal > 0: cellcdf['pqwait'].append(0.0 if mywaitcount==0 else mywaittotal/mywaitcount)

        # ave queue time per cell appward
        if mywaittotal > 0: cellcdf['pqcellwait'].append(0.0 if myproctotal==0 else mywaittotal/myproctotal)

        # queue len appward
        for tick in n.cellpql:
            length = n.cellpql[tick]
            if tick not in celltime['pqlen']: celltime['pqlen'][tick] = 0.0
            celltime['pqlen'][tick] += length
            pqlencounter += 1
            mylentotal += length
            mylencount += 1.0
            if args.all:
                if tick not in myctpqlen: myctpqlen[tick] = 0.0
                myctpqlen[tick] += length
                myccpqlen.append(length)
        if mylentotal > 0: cellcdf['pqlen'].append(0.0 if mylencount==0 else mylentotal/mylencount)

        myproctotal, myproccount, mywaittotal, mywaitcount, mylentotal, mylencount = 0.0, 0.0, 0.0, 0.0, 0.0, 0.0

        # cells processed exitward
        for tick in n.cellnqp:
            proc = n.cellnqp[tick]
            if tick not in celltime['nqproc']: celltime['nqproc'][tick] = 0.0
            celltime['nqproc'][tick] += proc
            myproctotal += proc
            myproccount += 1.0
            if args.all:
                if tick not in myctnqproc: myctnqproc[tick] = 0.0
                myctnqproc[tick] += proc
                myccnqproc.append(proc)
        if myproctotal > 0: cellcdf['nqproc'].append(0.0 if myproccount==0 else myproctotal/myproccount)

        # queue time exitward
        for tick in n.cellnqw:
            wait = n.cellnqw[tick]
            assert tick in n.cellnqp
            proc = n.cellnqp[tick]
            if tick not in celltime['nqwait']: celltime['nqwait'][tick] = 0.0
            celltime['nqwait'][tick] += wait
            mywaittotal += wait
            mywaitcount += 1.0
            if args.all:
                if tick not in myctnqwait: myctnqwait[tick] = 0.0
                myctnqwait[tick] += wait
                myccnqwait.append(wait)
                myccnqcellwait.append(0.0 if proc==0 else wait/proc)
        if mywaittotal > 0: cellcdf['nqwait'].append(0.0 if mywaitcount==0 else mywaittotal/mywaitcount)

        # ave queue time per cell exitward
        if mywaittotal > 0: cellcdf['nqcellwait'].append(0.0 if myproctotal==0 else mywaittotal/myproctotal)

        # queue len exitward
        for tick in n.cellnql:
            length = n.cellnql[tick]
            if tick not in celltime['nqlen']: celltime['nqlen'][tick] = 0.0
            celltime['nqlen'][tick] += length
            nqlencounter += 1
            mylentotal += length
            mylencount += 1.0
            if args.all:
                if tick not in myctnqlen: myctnqlen[tick] = 0.0
                myctnqlen[tick] += length
                myccnqlen.append(length)
        if mylentotal > 0: cellcdf['nqlen'].append(0.0 if mylencount==0 else mylentotal/mylencount)

        tokenstotal, tokenscount = 0.0, 0.0

        # token stats over time, and average per relay
        # global read
        for tick in n.tokengr:
            tokens = sum(n.tokengr[tick])/len(n.tokengr[tick])
            if tick not in tokentime['gread']: tokentime['gread'][tick] = []
            tokentime['gread'][tick].append(tokens)
            tokenstotal += tokens
            tokenscount += 1.0
            if args.all:
                if tick not in myttgread: myttgread[tick] = []
                myttgread[tick].append(tokens)
                mytcgread.append(tokens)
        if tokenstotal > 0: tokencdf['gread'].append(0.0 if tokenscount==0 else tokenstotal/tokenscount)

        tokenstotal, tokenscount = 0.0, 0.0

        # global write
        for tick in n.tokengw:
            tokens = sum(n.tokengw[tick])/len(n.tokengw[tick])
            if tick not in tokentime['gwrite']: tokentime['gwrite'][tick] = []
            tokentime['gwrite'][tick].append(tokens)
            tokenstotal += tokens
            tokenscount += 1.0
            if args.all:
                if tick not in myttgwrite: myttgwrite[tick] = []
                myttgwrite[tick].append(tokens)
                mytcgwrite.append(tokens)
        if tokenstotal > 0: tokencdf['gwrite'].append(0.0 if tokenscount==0 else tokenstotal/tokenscount)

        tokenstotal, tokenscount = 0.0, 0.0

        # token stats over time, and average per relay
        # relay read
        for tick in n.tokenrr:
            tokens = sum(n.tokenrr[tick])/len(n.tokenrr[tick])
            if tick not in tokentime['rread']: tokentime['rread'][tick] = []
            tokentime['rread'][tick].append(tokens)
            tokenstotal += tokens
            tokenscount += 1.0
            if args.all:
                if tick not in myttrread: myttrread[tick] = []
                myttrread[tick].append(tokens)
                mytcrread.append(tokens)
        if tokenstotal > 0: tokencdf['rread'].append(0.0 if tokenscount==0 else tokenstotal/tokenscount)

        tokenstotal, tokenscount = 0.0, 0.0

        # relay write
        for tick in n.tokenrw:
            tokens = sum(n.tokenrw[tick])/len(n.tokenrw[tick])
            if tick not in tokentime['rwrite']: tokentime['rwrite'][tick] = []
            tokentime['rwrite'][tick].append(tokens)
            tokenstotal += tokens
            tokenscount += 1.0
            if args.all:
                if tick not in myttrwrite: myttrwrite[tick] = []
                myttrwrite[tick].append(tokens)
                mytcrwrite.append(tokens)
        if tokenstotal > 0: tokencdf['rwrite'].append(0.0 if tokenscount==0 else tokenstotal/tokenscount)

        tokenstotal, tokenscount = 0.0, 0.0

        # token stats over time, and average per relay
        # orconn read
        for tick in n.tokenor:
            tokens = sum(n.tokenor[tick])/len(n.tokenor[tick])
            if tick not in tokentime['oread']: tokentime['oread'][tick] = []
            tokentime['oread'][tick].append(tokens)
            tokenstotal += tokens
            tokenscount += 1.0
            if args.all:
                if tick not in myttoread: myttoread[tick] = []
                myttoread[tick].append(tokens)
                mytcoread.append(tokens)
        if tokenstotal > 0: tokencdf['oread'].append(0.0 if tokenscount==0 else tokenstotal/tokenscount)

        tokenstotal, tokenscount = 0.0, 0.0

        # orconn write
        for tick in n.tokenow:
            tokens = sum(n.tokenow[tick])/len(n.tokenow[tick])
            if tick not in tokentime['owrite']: tokentime['owrite'][tick] = []
            tokentime['owrite'][tick].append(tokens)
            tokenstotal += tokens
            tokenscount += 1.0
            if args.all:
                if tick not in myttowrite: myttowrite[tick] = []
                myttowrite[tick].append(tokens)
                mytcowrite.append(tokens)
        if tokenstotal > 0: tokencdf['owrite'].append(0.0 if tokenscount==0 else tokenstotal/tokenscount)

        if args.all:
            for tokendata in [myttgread, myttgwrite, myttrread, myttrwrite, myttoread, myttowrite]:
                for tick in tokendata:
                    if len(tokendata[tick]) > 0: tokendata[tick] = sum(tokendata[tick])/len(tokendata[tick])
            # sort data for plotting
            myccpqproc.sort()
            myccnqproc.sort()
            myccpqwait.sort()
            myccnqwait.sort()
            myccpqcellwait.sort()
            myccnqcellwait.sort()
            myccpqlen.sort()
            myccnqlen.sort()
            mytcgread.sort()
            mytcgwrite.sort()
            mytcrread.sort()
            mytcrwrite.sort()
            mytcoread.sort()
            mytcowrite.sort()
            # fixup any missing ticks in data-over-time collections
            myctpqproc = fixupticks(myctpqproc)
            myctnqproc = fixupticks(myctnqproc)
            myctpqwait = fixupticks(myctpqwait)
            myctnqwait = fixupticks(myctnqwait)
            myctpqlen = fixupticks(myctpqlen)
            myctnqlen = fixupticks(myctnqlen)
            myttgread = fixupticks(myttgread)
            myttgwrite = fixupticks(myttgwrite)
            myttrread = fixupticks(myttrread)
            myttrwrite = fixupticks(myttrwrite)
            myttoread = fixupticks(myttoread)
            myttowrite = fixupticks(myttowrite)
            # save
            save(outputpath, "node/{0}/cells-pqproc.gz".format(name), myctpqproc.values())
            save(outputpath, "node/{0}/cells-nqproc.gz".format(name), myctnqproc.values())
            save(outputpath, "node/{0}/cells-pqwait.gz".format(name), myctpqwait.values())
            save(outputpath, "node/{0}/cells-nqwait.gz".format(name), myctnqwait.values())
            save(outputpath, "node/{0}/cells-pqlen.gz".format(name), myctpqlen.values())
            save(outputpath, "node/{0}/cells-nqlen.gz".format(name), myctnqlen.values())
            save(outputpath, "node/{0}/cells-pqproc-cdf.gz".format(name), myccpqproc)
            save(outputpath, "node/{0}/cells-nqproc-cdf.gz".format(name), myccnqproc)
            save(outputpath, "node/{0}/cells-pqwait-cdf.gz".format(name), myccpqwait)
            save(outputpath, "node/{0}/cells-nqwait-cdf.gz".format(name), myccnqwait)
            save(outputpath, "node/{0}/cells-pqcellwait-cdf.gz".format(name), myccpqcellwait)
            save(outputpath, "node/{0}/cells-nqcellwait-cdf.gz".format(name), myccnqcellwait)
            save(outputpath, "node/{0}/cells-pqlen-cdf.gz".format(name), myccpqlen)
            save(outputpath, "node/{0}/cells-nqlen-cdf.gz".format(name), myccnqlen)
            save(outputpath, "node/{0}/tokens-gread.gz".format(name), myttgread.values())
            save(outputpath, "node/{0}/tokens-gwrite.gz".format(name), myttgwrite.values())
            save(outputpath, "node/{0}/tokens-rread.gz".format(name), myttrread.values())
            save(outputpath, "node/{0}/tokens-rwrite.gz".format(name), myttrwrite.values())
            save(outputpath, "node/{0}/tokens-oread.gz".format(name), myttoread.values())
            save(outputpath, "node/{0}/tokens-owrite.gz".format(name), myttowrite.values())
            save(outputpath, "node/{0}/tokens-gread-cdf.gz".format(name), mytcgread)
            save(outputpath, "node/{0}/tokens-gwrite-cdf.gz".format(name), mytcgwrite)
            save(outputpath, "node/{0}/tokens-rread-cdf.gz".format(name), mytcrread)
            save(outputpath, "node/{0}/tokens-rwrite-cdf.gz".format(name), mytcrwrite)
            save(outputpath, "node/{0}/tokens-oread-cdf.gz".format(name), mytcoread)
            save(outputpath, "node/{0}/tokens-owrite-cdf.gz".format(name), mytcowrite)

    # compute ave lens instead of cumulative
    for tick in celltime['pqlen']:
        if pqlencounter > 0: celltime['pqlen'][tick] = celltime['pqlen'][tick]/pqlencounter
    for tick in celltime['nqlen']:
        if nqlencounter > 0: celltime['nqlen'][tick] = celltime['nqlen'][tick]/nqlencounter

    for tokendata in [tokentime['gread'], tokentime['gwrite'], tokentime['rread'], tokentime['rwrite'], tokentime['oread'], tokentime['owrite']]:
        for tick in tokendata:
            if len(tokendata[tick]) > 0: tokendata[tick] = sum(tokendata[tick])/len(tokendata[tick])

    # sort data for plotting
    # [d[x] for x in sorted(d.keys())] # sort values in dict d by keys
    circs['buildtime'].sort()
    circs['failtime'].sort()
    circs['runtime'].sort()
    strms['buildtime'].sort()
    strms['failtime'].sort()
    strms['runtime'].sort()
    cellcdf['pqproc'].sort()
    cellcdf['nqproc'].sort()
    cellcdf['pqwait'].sort()
    cellcdf['nqwait'].sort()
    cellcdf['pqcellwait'].sort()
    cellcdf['nqcellwait'].sort()
    cellcdf['pqlen'].sort()
    cellcdf['nqlen'].sort()
    tokencdf['gread'].sort()
    tokencdf['gwrite'].sort()
    tokencdf['rread'].sort()
    tokencdf['rwrite'].sort()
    tokencdf['oread'].sort()
    tokencdf['owrite'].sort()
    # fixup any missing ticks in data-over-time collections
    circs['fail'] = fixupticks(circs['fail'])
    circs['build'] = fixupticks(circs['build'])
    strms['fail'] = fixupticks(strms['fail'])
    strms['build'] = fixupticks(strms['build'])
    celltime['pqproc'] = fixupticks(celltime['pqproc'])
    celltime['nqproc'] = fixupticks(celltime['nqproc'])
    celltime['pqwait'] = fixupticks(celltime['pqwait'])
    celltime['nqwait'] = fixupticks(celltime['nqwait'])
    celltime['pqlen'] = fixupticks(celltime['pqlen'])
    celltime['nqlen'] = fixupticks(celltime['nqlen'])
    tokentime['gread'] = fixupticks(tokentime['gread'])
    tokentime['gwrite'] = fixupticks(tokentime['gwrite'])
    tokentime['rread'] = fixupticks(tokentime['rread'])
    tokentime['rwrite'] = fixupticks(tokentime['rwrite'])
    tokentime['oread'] = fixupticks(tokentime['oread'])
    tokentime['owrite'] = fixupticks(tokentime['owrite'])

    # save our aggregated data
    save(outputpath, "time-virtual.gz", times['virtual'])
    save(outputpath, "time-real.gz", times['real'])

    save(outputpath, "ttfb-im.gz", ttfb['im'])
    save(outputpath, "ttlb-im.gz", ttlb['im'])

    save(outputpath, "ttfb-web.gz", ttfb['web'])
    save(outputpath, "ttlb-web.gz", ttlb['web'])

    save(outputpath, "ttfb-bulk.gz", ttfb['bulk'])
    save(outputpath, "ttlb-bulk.gz", ttlb['bulk'])

    save(outputpath, "ttfb-p2p.gz", ttfb['p2p'])
    save(outputpath, "ttlb-p2p.gz", ttlb['p2p'])

    save(outputpath, "ttfb-perf50k.gz", ttfb['perf50k'])
    save(outputpath, "ttlb-perf50k.gz", ttlb['perf50k'])

    save(outputpath, "ttfb-perf1m.gz", ttfb['perf1m'])
    save(outputpath, "ttlb-perf1m.gz", ttlb['perf1m'])

    save(outputpath, "ttfb-perf5m.gz", ttfb['perf5m'])
    save(outputpath, "ttlb-perf5m.gz", ttlb['perf5m'])

    save(outputpath, "ttfb-other.gz", ttfb['other'])
    save(outputpath, "ttlb-other.gz", ttlb['other'])

    save(outputpath, "tput-read.gz", tput['read'].values())
    save(outputpath, "tput-write.gz", tput['write'].values())

    save(outputpath, "mem-alloc.gz", mem['alloc'].values())
    save(outputpath, "mem-dealloc.gz", mem['dealloc'].values())
    save(outputpath, "mem-total.gz", mem['total'].values())

    save(outputpath, "circs-build.gz", circs['build'].values())
    save(outputpath, "circs-fail.gz", circs['fail'].values())
    save(outputpath, "circs-buildtime-cdf.gz", circs['buildtime'])
    save(outputpath, "circs-failtime-cdf.gz", circs['failtime'])
    save(outputpath, "circs-runtime-cdf.gz", circs['runtime'])
    save(outputpath, "circs-hops.gz", sorted(circs['hops'].values()))

    save(outputpath, "strms-build.gz", strms['build'].values())
    save(outputpath, "strms-fail.gz", strms['fail'].values())
    save(outputpath, "strms-buildtime-cdf.gz", strms['buildtime'])
    save(outputpath, "strms-failtime-cdf.gz", strms['failtime'])
    save(outputpath, "strms-runtime-cdf.gz", strms['runtime'])

    save(outputpath, "cells-pqproc.gz", celltime['pqproc'].values())
    save(outputpath, "cells-nqproc.gz", celltime['nqproc'].values())
    save(outputpath, "cells-pqwait.gz", celltime['pqwait'].values())
    save(outputpath, "cells-nqwait.gz", celltime['nqwait'].values())
    save(outputpath, "cells-pqlen.gz", celltime['pqlen'].values())
    save(outputpath, "cells-nqlen.gz", celltime['nqlen'].values())
    save(outputpath, "cells-pqproc-cdf.gz", cellcdf['pqproc'])
    save(outputpath, "cells-nqproc-cdf.gz", cellcdf['nqproc'])
    save(outputpath, "cells-pqwait-cdf.gz", cellcdf['pqwait'])
    save(outputpath, "cells-nqwait-cdf.gz", cellcdf['nqwait'])
    save(outputpath, "cells-pqcellwait-cdf.gz", cellcdf['pqcellwait'])
    save(outputpath, "cells-nqcellwait-cdf.gz", cellcdf['nqcellwait'])
    save(outputpath, "cells-pqlen-cdf.gz", cellcdf['pqlen'])
    save(outputpath, "cells-nqlen-cdf.gz", cellcdf['nqlen'])

    save(outputpath, "tokens-gread.gz", tokentime['gread'].values())
    save(outputpath, "tokens-gwrite.gz", tokentime['gwrite'].values())
    save(outputpath, "tokens-rread.gz", tokentime['rread'].values())
    save(outputpath, "tokens-rwrite.gz", tokentime['rwrite'].values())
    save(outputpath, "tokens-oread.gz", tokentime['oread'].values())
    save(outputpath, "tokens-owrite.gz", tokentime['owrite'].values())

    save(outputpath, "tokens-gread-cdf.gz", tokencdf['gread'])
    save(outputpath, "tokens-gwrite-cdf.gz", tokencdf['gwrite'])
    save(outputpath, "tokens-rread-cdf.gz", tokencdf['rread'])
    save(outputpath, "tokens-rwrite-cdf.gz", tokencdf['rwrite'])
    save(outputpath, "tokens-oread-cdf.gz", tokencdf['oread'])
    save(outputpath, "tokens-owrite-cdf.gz", tokencdf['owrite'])

    # quick aggregate stats
    print "Some quick stats:"
    with open("{0}/parse-stats".format(outputpath), 'wb') as f:
        countmsg = "\tdownload counts: {0} web, {1} bulk, {2} im, {3} p2p, {4} perf50k, {5} perf1m, {6} perf5m, {7} other".format(len(ttlb['web']), len(ttlb['bulk']), len(ttlb['im']), len(ttlb['p2p']), len(ttlb['perf50k']), len(ttlb['perf1m']), len(ttlb['perf5m']), len(ttlb['other']))
        print >>f, countmsg
        print countmsg
        
        if len(tput['read']) > 0:
            tputreadmsg = "\ttput read MiB/s: {0} min, {1} max, {2} median, {3} mean".format(min(tput['read'].values()), max(tput['read'].values()), numpy.median(tput['read'].values()), numpy.mean(tput['read'].values()))
            print >>f, tputreadmsg
            print tputreadmsg
        
        if len(tput['write']) > 0:
            tputwritemsg = "\ttput write MiB/s: {0} min, {1} max, {2} median, {3} mean".format(min(tput['write'].values()), max(tput['write'].values()), numpy.median(tput['write'].values()), numpy.mean(tput['write'].values()))
            print >>f, tputwritemsg
            print tputwritemsg

        if len(mem['alloc']) > 0:
            memallocmsg = "\tmem alloc MiB: {0} min, {1} max, {2} median, {3} mean".format(min(mem['alloc'].values()), max(mem['alloc'].values()), numpy.median(mem['alloc'].values()), numpy.mean(mem['alloc'].values()))
            print >>f, memallocmsg
            print memallocmsg

        if len(mem['dealloc']) > 0:
            memdeallocmsg = "\tmem dealloc MiB: {0} min, {1} max, {2} median, {3} mean".format(min(mem['dealloc'].values()), max(mem['dealloc'].values()), numpy.median(mem['dealloc'].values()), numpy.mean(mem['dealloc'].values()))
            print >>f, memdeallocmsg
            print memdeallocmsg

        if len(mem['total']) > 0:
            memtotalmsg = "\tmem usage MiB: {0} min, {1} max, {2} median, {3} mean".format(min(mem['total'].values()), max(mem['total'].values()), numpy.median(mem['total'].values()), numpy.mean(mem['total'].values()))
            print >>f, memtotalmsg
            print memtotalmsg
        
        if len(times['virtual']) > 0:
            timemsg = "\ttiming hours: {0} virtual in {1} real".format(times['virtual'][-1], times['real'][-1])
            print >>f, timemsg
            print timemsg

    with open("{0}/circuit-choices".format(outputpath), 'wb') as f:
        total = sum(circs['hops'].values())
        print >>f, "RELAY,HOPS%"
        for relay in sorted(circs['hops'].keys()):
            p = circs['hops'][relay]/total
            print >>f, "{0},{1}".format(relay, p)

    print "Done!"

def plot(args):
    graphpath = os.path.abspath(os.path.expanduser(args.graphpath))
    prefix = args.prefix
    formats = args.format.strip().split(",")
    experiments = args.experiments
    stitle = args.title if args.title != "" else None
    stitlesize = SUPERTITLE_SIZE
    titlesize = TITLE_SIZE
    maseconds = 60.0 # seconds moving average

    ## load the data archives
    data = {}
    for e in experiments:
        data[e[0]] = {
            'ttfb-im': load("{0}/ttfb-im.gz".format(e[0])), 
            'ttlb-im': load("{0}/ttlb-im.gz".format(e[0])), 
            'ttfb-web': load("{0}/ttfb-web.gz".format(e[0])), 
            'ttlb-web': load("{0}/ttlb-web.gz".format(e[0])), 
            'ttfb-bulk': load("{0}/ttfb-bulk.gz".format(e[0])), 
            'ttlb-bulk': load("{0}/ttlb-bulk.gz".format(e[0])), 
            'ttfb-p2p': load("{0}/ttfb-p2p.gz".format(e[0])), 
            'ttlb-p2p': load("{0}/ttlb-p2p.gz".format(e[0])), 
            'ttfb-perf50k': load("{0}/ttfb-perf50k.gz".format(e[0])), 
            'ttlb-perf50k': load("{0}/ttlb-perf50k.gz".format(e[0])), 
            'ttfb-perf1m': load("{0}/ttfb-perf1m.gz".format(e[0])), 
            'ttlb-perf1m': load("{0}/ttlb-perf1m.gz".format(e[0])), 
            'ttfb-perf5m': load("{0}/ttfb-perf5m.gz".format(e[0])), 
            'ttlb-perf5m': load("{0}/ttlb-perf5m.gz".format(e[0])), 
            'ttfb-other': load("{0}/ttfb-other.gz".format(e[0])), 
            'ttlb-other': load("{0}/ttlb-other.gz".format(e[0])), 
            'time-virtual' : load("{0}/time-virtual.gz".format(e[0])),
            'time-real' : load("{0}/time-real.gz".format(e[0])),
            'tput-read' : load("{0}/tput-read.gz".format(e[0])),
            'tput-write' : load("{0}/tput-write.gz".format(e[0])),
            'mem-alloc' : load("{0}/mem-alloc.gz".format(e[0])),
            'mem-dealloc' : load("{0}/mem-dealloc.gz".format(e[0])),
            'mem-total' : load("{0}/mem-total.gz".format(e[0])),
            'circs-build' : load("{0}/circs-build.gz".format(e[0])),
            'circs-fail' : load("{0}/circs-fail.gz".format(e[0])),
            'circs-buildtime-cdf' : load("{0}/circs-buildtime-cdf.gz".format(e[0])),
            'circs-failtime-cdf' : load("{0}/circs-failtime-cdf.gz".format(e[0])),
            'circs-runtime-cdf' : load("{0}/circs-runtime-cdf.gz".format(e[0])),
            'circs-hops' : load("{0}/circs-hops.gz".format(e[0])),
            'strms-build' : load("{0}/strms-build.gz".format(e[0])),
            'strms-fail' : load("{0}/strms-fail.gz".format(e[0])),
            'strms-buildtime-cdf' : load("{0}/strms-buildtime-cdf.gz".format(e[0])),
            'strms-failtime-cdf' : load("{0}/strms-failtime-cdf.gz".format(e[0])),
            'strms-runtime-cdf' : load("{0}/strms-runtime-cdf.gz".format(e[0])),
            'socket-buffers' : loadpickle("{0}/socket-buffers.gz".format(e[0])),
            'cells-pqproc' : load("{0}/cells-pqproc.gz".format(e[0])),
            'cells-nqproc' : load("{0}/cells-nqproc.gz".format(e[0])),
            'cells-pqwait' : load("{0}/cells-pqwait.gz".format(e[0])),
            'cells-nqwait' : load("{0}/cells-nqwait.gz".format(e[0])),
            'cells-pqlen' : load("{0}/cells-pqlen.gz".format(e[0])),
            'cells-nqlen' : load("{0}/cells-nqlen.gz".format(e[0])),
            'cells-pqproc-cdf' : load("{0}/cells-pqproc-cdf.gz".format(e[0])),
            'cells-nqproc-cdf' : load("{0}/cells-nqproc-cdf.gz".format(e[0])),
            'cells-pqwait-cdf' : load("{0}/cells-pqwait-cdf.gz".format(e[0])),
            'cells-nqwait-cdf' : load("{0}/cells-nqwait-cdf.gz".format(e[0])),
            'cells-pqcellwait-cdf' : load("{0}/cells-pqcellwait-cdf.gz".format(e[0])),
            'cells-nqcellwait-cdf' : load("{0}/cells-nqcellwait-cdf.gz".format(e[0])),
            'cells-pqlen-cdf' : load("{0}/cells-pqlen-cdf.gz".format(e[0])),
            'cells-nqlen-cdf' : load("{0}/cells-nqlen-cdf.gz".format(e[0])),
            'tokens-gread' : load("{0}/tokens-gread.gz".format(e[0])),
            'tokens-gwrite' : load("{0}/tokens-gwrite.gz".format(e[0])),
            'tokens-rread' : load("{0}/tokens-rread.gz".format(e[0])),
            'tokens-rwrite' : load("{0}/tokens-rwrite.gz".format(e[0])),
            'tokens-oread' : load("{0}/tokens-oread.gz".format(e[0])),
            'tokens-owrite' : load("{0}/tokens-owrite.gz".format(e[0])),
            'tokens-gread-cdf' : load("{0}/tokens-gread-cdf.gz".format(e[0])),
            'tokens-gwrite-cdf' : load("{0}/tokens-gwrite-cdf.gz".format(e[0])),
            'tokens-rread-cdf' : load("{0}/tokens-rread-cdf.gz".format(e[0])),
            'tokens-rwrite-cdf' : load("{0}/tokens-rwrite-cdf.gz".format(e[0])),
            'tokens-oread-cdf' : load("{0}/tokens-oread-cdf.gz".format(e[0])),
            'tokens-owrite-cdf' : load("{0}/tokens-owrite-cdf.gz".format(e[0])),
        }
        
        data[e[0]]['nodes'] = {}
        if os.path.exists("{0}/node".format(e[0])):
            for node in os.listdir("{0}/node".format(e[0])):
                data[e[0]]['nodes'][node] = {
                    'circs-build' : load("{0}/node/{1}/circs-build.gz".format(e[0], node)),
                    'circs-buildtime-cdf' : load("{0}/node/{1}/circs-buildtime-cdf.gz".format(e[0], node)),
                    'circs-hops' : load("{0}/node/{1}/circs-hops.gz".format(e[0], node)),
                    'socket-buffers' : loadpickle("{0}/node/{1}/socket-buffers.gz".format(e[0], node)),
                    'tput-read' : load("{0}/node/{1}/tput-read.gz".format(e[0], node)),
                    'tput-write' : load("{0}/node/{1}/tput-write.gz".format(e[0], node)),
                    'mem-alloc' : load("{0}/node/{1}/mem-alloc.gz".format(e[0], node)),
                    'mem-dealloc' : load("{0}/node/{1}/mem-dealloc.gz".format(e[0], node)),
                    'mem-total' : load("{0}/node/{1}/mem-total.gz".format(e[0], node)),
                }
        
    if not os.path.exists(graphpath): os.mkdir(graphpath)
    savedfigures = []

    ######################
    # run timings        #
    ######################

    print "Generating timing graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['time-real']) > 0 and len(data[e[0]]['time-virtual']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x = data[e[0]]['time-real']
            y = data[e[0]]['time-virtual']
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Shadow Experiment Timing", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Real Time (h)")
        pylab.ylabel("Virtual Time (h)")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-timing.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # time to first byte #
    ######################

    print "Generating time to first byte graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-im']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-im'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("IM 1 KiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-im.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-web']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-web'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Web 320 KiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-web.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-bulk']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-bulk'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Bulk 5 MiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-bulk.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-p2p']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-p2p'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("P2P 16 KiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-p2p.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-perf50k']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-perf50k'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("ShadowPerf 50KiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-perf50k.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-perf1m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-perf1m'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("ShadowPerf 1 MiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-perf1m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-perf5m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-perf5m'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("ShadowPerf 5 MiB Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0, xmax=10.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-perf5m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttfb-other']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttfb-other'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Other File Clients First Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttfb-other.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # time to last byte  #
    ######################

    print "Generating time to last byte graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-im']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-im'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("IM 1 KiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-im.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-web']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-web'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Web 320 KiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-web.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-bulk']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-bulk'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Bulk 5 MiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-bulk.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-p2p']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-p2p'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("P2P 16 KiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-p2p.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-perf50k']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-perf50k'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("ShadowPerf 50KiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-perf50k.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-perf1m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-perf1m'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("ShadowPerf 1 MiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-perf1m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-perf5m']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-perf5m'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("ShadowPerf 5 MiB Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-perf5m.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['ttlb-other']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['ttlb-other'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Other File Clients Last Byte", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-ttlb-other.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # load               #
    ######################

    print "Generating load/throughput graphs"

    ## load over time

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tput-read']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            y = data[e[0]]['tput-read']
            x = xrange(len(y))
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Network Data Read Over Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Read (MiB/s)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tput-read.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tput-write']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            y = data[e[0]]['tput-write']
            x = xrange(len(y))
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Network Data Written Over Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Writen (MiB/s)")
        pylab.legend(loc="upper left")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tput-write.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ## pie charts

    for e in experiments:
        web_num = len(data[e[0]]['ttlb-web'])
        bulk_num = len(data[e[0]]['ttlb-bulk'])
        im_num = len(data[e[0]]['ttlb-im'])
        p2p_num = len(data[e[0]]['ttlb-p2p'])
        perf50k_num = len(data[e[0]]['ttlb-perf50k'])
        perf1m_num = len(data[e[0]]['ttlb-perf1m'])
        perf5m_num = len(data[e[0]]['ttlb-perf5m'])

        ## we generally dont know the size of these downloads
        #other_num = len(data[e[0]]['ttlb-other'])

        total_num = web_num + bulk_num + im_num + p2p_num + perf50k_num + perf1m_num + perf5m_num

        web_gib = web_num * 320.0 / (1024.0*1024.0)
        bulk_gib = bulk_num * 5120.0 / (1024.0*1024.0)
        im_gib = im_num * 1.0 / (1024.0*1024.0)
        p2p_gib = p2p_num * 16.0 / (1024.0*1024.0)
        perf50k_gib = perf50k_num * 50.0 / (1024.0*1024.0)
        perf1m_gib = perf1m_num * 1024.0 / (1024.0*1024.0)
        perf5m_gib = perf5m_num * 5120.0 / (1024.0*1024.0)

        total_gib = web_gib + bulk_gib + im_gib + p2p_gib + perf50k_gib + perf1m_gib + perf5m_gib

        if total_num > 0:
            pylab.figure()
            pylab.axes([0.2, 0.05, 0.6, 0.8])

            labels, fractions = [], []
            if web_num > 0: 
                fractions.append(web_gib/total_gib)
                labels.append("Web\n{1}{2}\n{0}GiB".format(decimals(web_gib), decimals(100.0*fractions[-1]), '%'))
            if bulk_num > 0: 
                fractions.append(bulk_gib/total_gib)
                labels.append("Bulk\n{1}{2}\n{0}GiB".format(decimals(bulk_gib), decimals(100.0*fractions[-1]), '%'))
            if im_num > 0: 
                fractions.append(im_gib/total_gib)
                labels.append("IM\n{1}{2}\n{0}GiB".format(decimals(im_gib), decimals(100.0*fractions[-1]), '%'))
            if p2p_num > 0: 
                fractions.append(p2p_gib/total_gib)
                labels.append("P2P\n{1}{2}\n{0}GiB".format(decimals(p2p_gib), decimals(100.0*fractions[-1]), '%'))
            if perf50k_num > 0: 
                fractions.append(perf50k_gib/total_gib)
                labels.append("Perf50K\n{1}{2}\n{0}GiB".format(decimals(perf50k_gib), decimals(100.0*fractions[-1]), '%'))
            if perf1m_num > 0: 
                fractions.append(perf1m_gib/total_gib)
                labels.append("Perf1M\n{1}{2}\n{0}GiB".format(decimals(perf1m_gib), decimals(100.0*fractions[-1]), '%'))
            if perf5m_num > 0: 
                fractions.append(perf5m_gib/total_gib)
                labels.append("Perf5M\n{1}{2}\n{0}GiB".format(decimals(perf5m_gib), decimals(100.0*fractions[-1]), '%'))

            # show fractions of pie with autopct='%1.2f%%'
            patches = pylab.pie(fractions, explode=[0.1]*len(fractions), autopct=None, shadow=True)

            leg = pylab.legend(patches, labels, loc=(-0.3, -0.05), labelspacing=0.8)
            pylab.setp(leg.get_texts(), fontsize='6')

            if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
            pylab.title("Total Client Load, {0}: {1} GiB".format(e[1], decimals(total_gib)), fontsize=titlesize, x='1.0', ha='right')
            figname = "{0}/{1}-{2}-clientload.pdf".format(graphpath, prefix, e[1])
            savedfigures.append(figname)
            pylab.savefig(figname)  

    ######################
    # memory             #
    ######################

    print "Generating memory usage graphs"

    ## memory usage over time

    doplot = False
    for e in experiments:
        if len(data[e[0]]['mem-alloc']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            y = data[e[0]]['mem-alloc']
            x = xrange(len(y))
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Plug-in Memory Allocated Over Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Allocated (MiB)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-mem-alloc.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['mem-dealloc']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            y = data[e[0]]['mem-dealloc']
            x = xrange(len(y))
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Plug-in Memory Deallocated Over Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Deallocated (MiB)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-mem-dealloc.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['mem-total']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            y = data[e[0]]['mem-total']
            x = xrange(len(y))
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Plug-in Memory Usage Over Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Used (MiB)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-mem-total.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # circuits           #
    ######################

    print "Generating circuit graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['circs-build']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            d = data[e[0]]['circs-build']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Circuits Built Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Built (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-circs-build.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['circs-buildtime-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['circs-buildtime-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Circuit Build Times", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time to Build Circuit (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-circs-buildtime-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['circs-fail']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['circs-fail']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Circuits Failed Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Failed (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-circs-fail.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['circs-failtime-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['circs-failtime-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Circuit Failure Times", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time to Fail Circuit (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-circs-failtime-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['circs-runtime-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['circs-runtime-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Circuit Running Times", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time Circuit Open (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-circs-runtime-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['circs-hops']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        total = 0.0
        data[e[0]]['circs-hops'].sort()
        for x in data[e[0]]['circs-hops']: total += x
        for e in experiments:
            y = [x/total for x in data[e[0]]['circs-hops']]
            x = xrange(len(y))
            pylab.scatter(x, y, s=5, c=styles.next()[0], label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Path Selections", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Relay (index)")
        pylab.ylabel("Total Circuit Appearances (\%)")
        pylab.ylim(ymin=0.0)
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="upper left")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-circs-hops.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # streams            #
    ######################

    print "Generating stream graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['strms-build']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['strms-build']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Streams Built Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Built (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-strms-build.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['strms-buildtime-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['strms-buildtime-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Stream Connect Times", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time to Establish Stream (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.xlim(xmin=0.0)
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-strms-buildtime-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['strms-fail']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['strms-fail']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Streams Failed Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Total Failed (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-strms-fail.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['strms-failtime-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['strms-failtime-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Stream Failure Times", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time to Fail Stream (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-strms-failtime-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['strms-runtime-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['strms-runtime-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Stream Running Times", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time Stream Open (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-strms-runtime-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)
        
    ######################
    # buffer stats       #
    ######################
    print "Generating socket buffer graphs"
    

    doplot = False
    for e in experiments:
        if len(data[e[0]]['nodes']) > 0: doplot = True
    if doplot:    
        for e in experiments:
            for node in data[e[0]]['nodes'].keys():
                buffers = data[e[0]]['nodes'][node]['socket-buffers']
                total = {}
                for descriptor in buffers:
                    tick = [d[0] / 60.0 for d in buffers[descriptor]]
                    input_buffer_length = [d[1] / 1024.0 for d in buffers[descriptor]]
                    input_buffer_size = [d[2] / 1024.0 for d in buffers[descriptor]]
                    input_buffer_space = [(d[2] - d[1]) / 1024.0 for d in buffers[descriptor]]
                    output_buffer_length = [d[3] / 1024.0 for d in buffers[descriptor]]
                    output_buffer_size = [d[4] / 1024.0 for d in buffers[descriptor]]
                    output_buffer_space = [(d[4] - d[3]) / 1024.0 for d in buffers[descriptor]]
                    
                    ####
                    # Input Buffers
                    ####
                    
                    
                    if float(max(input_buffer_length)) / float(max(input_buffer_size)) > 0.05:
                        pylab.figure()
                        styles = itertools.cycle(formats)
                        pylab.plot(tick, input_buffer_size, styles.next(), label='Buffer Size')
                        pylab.plot(tick, input_buffer_length, styles.next(), label='Buffer Length')
                        pylab.title("{0} - {1} - Input Buffer".format(node, descriptor), fontsize=titlesize, x="1.0", ha='right')
                        pylab.xlabel("Tick (m)")
                        pylab.ylabel("Buffer Length (KB)")
                        pylab.ylim(0, max(input_buffer_size) * 1.25)
                        pylab.ticklabel_format(style='plain',axis='y')
                        pylab.legend(loc="upper left")
                        figname = "{0}/{1}/sockets/{3}/input-buffer-size.pdf".format(graphpath, node, prefix, descriptor.split(' ')[0])
                        dir = figname[:figname.rindex('/')]
                        if not os.path.exists(dir): os.makedirs(dir)
                        pylab.savefig(figname)
                        
                        pylab.figure()
                        styles = itertools.cycle(formats)
                        pylab.plot(tick, input_buffer_space, styles.next(), label='Buffer Space')
                        pylab.title("{0} - {1} - Input Buffer".format(node, descriptor), fontsize=titlesize, x="1.0", ha='right')
                        pylab.xlabel("Tick (m)")
                        pylab.ylabel("Buffer Length (KB)")
                        pylab.ylim(0, max(input_buffer_space) * 1.25)
                        pylab.ticklabel_format(style='plain',axis='y')
                        pylab.legend(loc="upper left")
                        figname = "{0}/{1}/sockets/{3}/input-buffer-space.pdf".format(graphpath, node, prefix, descriptor.split(' ')[0])
                        dir = figname[:figname.rindex('/')]
                        if not os.path.exists(dir): os.makedirs(dir)
                        pylab.savefig(figname)
                    
                    ####
                    # Output Buffers
                    ####
                    
                    if float(max(output_buffer_length)) / float(max(output_buffer_size)) > 0.05:
                        pylab.figure()
                        styles = itertools.cycle(formats)
                        pylab.plot(tick, output_buffer_size, styles.next(), label='Buffer Size')
                        pylab.plot(tick, output_buffer_length, styles.next(), label='Buffer Length')
                        pylab.title("{0} - {1} - Output Buffer".format(node, descriptor), fontsize=titlesize, x="1.0", ha='right')
                        pylab.xlabel("Tick (m)")
                        pylab.ylabel("Buffer Length (KB)")
                        pylab.ylim(0, max(output_buffer_size) * 1.25)
                        pylab.ticklabel_format(style='plain',axis='y')
                        pylab.legend(loc="upper left")
                        figname = "{0}/{1}/sockets/{3}/output-buffer-size.pdf".format(graphpath, node, prefix, descriptor.split(' ')[0])
                        dir = figname[:figname.rindex('/')]
                        if not os.path.exists(dir): os.makedirs(dir)
                        pylab.savefig(figname)
                        
                        pylab.figure()
                        styles = itertools.cycle(formats)
                        pylab.plot(tick, output_buffer_space, styles.next(), label='Buffer Space')
                        pylab.title("{0} - {1} - Output Buffer".format(node, descriptor), fontsize=titlesize, x="1.0", ha='right')
                        pylab.xlabel("Tick (m)")
                        pylab.ylabel("Buffer Length (KB)")
                        pylab.ylim(0, max(output_buffer_space) * 1.25)
                        pylab.ticklabel_format(style='plain',axis='y')
                        pylab.legend(loc="upper left")
                        figname = "{0}/{1}/sockets/{3}/output-buffer-space.pdf".format(graphpath, node, prefix, descriptor.split(' ')[0])
                        dir = figname[:figname.rindex('/')]
                        if not os.path.exists(dir): os.makedirs(dir)
                        #savedfigures.append(figname)
                        pylab.savefig(figname)
                
                
        

    ######################
    # cell queue stats   #
    ######################

    print "Generating circuit queue graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqproc']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['cells-pqproc']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Total Appward Cells Processed Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Cells Processed (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-pqproc.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqproc']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['cells-nqproc']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Total Exitward Cells Processed Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Cells Processed (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-nqproc.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqwait']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['cells-pqwait']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Total Appward Circuit Queue Time Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Time Queued (ms, 10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-pqwait.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqwait']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['cells-nqwait']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Total Exitward Circuit Queue Time Per Second", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Time Queued (ms, 10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-nqwait.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqproc']) > 0 and len(data[e[0]]['cells-pqwait']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            avewait = [0.0 if data[e[0]]['cells-pqproc'][i]==0 else float(data[e[0]]['cells-pqwait'][i])/float(data[e[0]]['cells-pqproc'][i]) for i in xrange(len(data[e[0]]['cells-pqproc']))]
            d = avewait
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Appward Cell Queue Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Time Queued (ms, 10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-pqcellwait.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqproc']) > 0 and len(data[e[0]]['cells-nqwait']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            avewait = [0.0 if data[e[0]]['cells-nqproc'][i]==0 else float(data[e[0]]['cells-nqwait'][i])/float(data[e[0]]['cells-nqproc'][i]) for i in xrange(len(data[e[0]]['cells-nqproc']))]
            d = avewait
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Exitward Cell Queue Time", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Time Queued (ms, 10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-nqcellwait.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqlen']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['cells-pqlen']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Appward Circuit Queue Length", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Mean Cells in Queue (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-pqlen.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqlen']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['cells-nqlen']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Exitward Circuit Queue Length", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Mean Cells in Queue (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-cells-nqlen.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqproc-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-pqproc-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Appward Cells Processed Per Second Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Cells Processed")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-pqproc-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqproc-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-nqproc-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Exitward Cells Processed Per Second Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Cells Processed")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-nqproc-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqwait-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-pqwait-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Appward Circuit Queue Time Per Second Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Queue Time (ms)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-pqwait-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqwait-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments: 
            x, y = getcdf(data[e[0]]['cells-nqwait-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Exitward Circuit Queue Time Per Second Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time Queued (ms)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-nqwait-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqcellwait-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-pqcellwait-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Appward Cell Queue Time Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time Queued (ms)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-pqcellwait-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqcellwait-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-nqcellwait-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Exitward Cell Queue Time Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Time Queued (ms)")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-nqcellwait-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-pqlen-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-pqlen-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Appward Circuit Queue Length Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Cells in Queue")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-pqlen-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['cells-nqlen-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['cells-nqlen-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Exitward Circuit Queue Length Per Relay", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Cells in Queue")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-cells-nqlen-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    ######################
    # token stats        #
    ######################

    print "Generating token bucket graphs"

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-gread']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['tokens-gread']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Global Read Bucket", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Tokens Remaining (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tokens-gread.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-gwrite']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['tokens-gwrite']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Global Write Bucket", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Tokens Remaining (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tokens-gwrite.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-rread']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['tokens-rread']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Relay Read Bucket", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Tokens Remaining (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tokens-rread.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-rwrite']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['tokens-rwrite']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Relay Write Bucket", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Tokens Remaining (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tokens-rwrite.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-oread']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['tokens-oread']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in ORConn Read Bucket", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Tokens Remaining (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tokens-oread.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-owrite']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            d = data[e[0]]['tokens-owrite']
            y = pylab.mlab.movavg(d, maseconds)
            x = [i/60.0 for i in xrange(len(y))]
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in ORConn Write Bucket", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tick (m)")
        pylab.ylabel("Tokens Remaining (10 s ma)")
        pylab.legend(loc="lower right")
        pylab.subplots_adjust(left=0.15)
        figname = "{0}/{1}-tokens-owrite.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-gread-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['tokens-gread-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Global Read Bucket Per Relay Per Refill", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tokens Remaining")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-tokens-gread-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-gwrite-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['tokens-gwrite-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Global Write Bucket Per Relay Per Refill", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tokens Remaining")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-tokens-gwrite-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-rread-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['tokens-rread-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Relay Read Bucket Per Relay Per Refill", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tokens Remaining")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-tokens-rread-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-rwrite-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['tokens-rwrite-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in Relay Write Bucket Per Relay Per Refill", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tokens Remaining")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-tokens-rwrite-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-oread-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['tokens-oread-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in ORConn Read Bucket Per Relay Per Refill", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tokens Remaining")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-tokens-oread-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

    doplot = False
    for e in experiments:
        if len(data[e[0]]['tokens-owrite-cdf']) > 0: doplot = True
    if doplot:    
        pylab.figure()
        styles = itertools.cycle(formats)
        for e in experiments:
            x, y = getcdf(data[e[0]]['tokens-owrite-cdf'])
            pylab.plot(x, y, styles.next(), label=e[1])
        if stitle is not None: pylab.suptitle(stitle, fontsize=stitlesize)
        pylab.title("Mean Tokens in ORConn Write Bucket Per Relay Per Refill", fontsize=titlesize, x='1.0', ha='right')
        pylab.xlabel("Tokens Remaining")
        pylab.ylabel("Cumulative Fraction")
        pylab.legend(loc="lower right")
        figname = "{0}/{1}-tokens-owrite-cdf.pdf".format(graphpath, prefix)
        savedfigures.append(figname)
        pylab.savefig(figname)

#########################

    ## concat the subgraphs into a single .pdf
    if which('pdftk') is not None:
        print "Concatenating the graphs from {0} using the 'pdftk' program".format(graphpath)
        cmd = list(savedfigures)
        cmd.insert(0, "pdftk")
        name = "{0}-combined.pdf".format(prefix)
        cmd.extend(["cat", "output", name])
        subprocess.call(cmd)
        print "Successfully created '{0}'!".format(name)
    print "Done!"

# helper - set axis in scientific format
def setsciformat(setx=True, sety=True):
    # Scientific notation is used for data < 10^-n or data >= 10^m, 
    # where n and m are the power limits set using set_powerlimits((n,m)).
    sciform = pylab.ScalarFormatter(useMathText=True)
    sciform.set_scientific(True)
    sciform.set_powerlimits((-3, 3))
    if setx: pylab.gca().xaxis.set_major_formatter(sciform)
    if sety: pylab.gca().xaxis.set_major_formatter(sciform)

# helper - save files
def save(outputpath, filename, data):
    if len(data) > 0:
        p = os.path.abspath(os.path.expanduser("{0}/{1}".format(outputpath, filename)))
        dir = p[:p.rindex('/')]
        if not os.path.exists(dir): os.makedirs(dir)
        print "Saving data to '{0}'".format(p)
        numpy.savetxt(p, data)

## helper - load the data in the correct format for plotting
def load(path):
    p = os.path.abspath(os.path.expanduser(path))
    if not os.path.exists(p): return []
    print "Loading data from '{0}'".format(p)
    data = numpy.loadtxt(p)
    return numpy.atleast_1d(data).tolist()

# helper - save files with pick
def savepickle(outputpath, filename, data):
    p = os.path.abspath(os.path.expanduser("{0}/{1}".format(outputpath, filename)))
    dir = p[:p.rindex('/')]
    if not os.path.exists(dir): os.makedirs(dir)
    print "Saving data to '{0}'".format(p)
    fp = gzip.open(p,'wb')
    cPickle.dump(data, fp)
    fp.close()
    
# helper - load the data from pickle file
def loadpickle(path):
    p = os.path.abspath(os.path.expanduser(path))
    if not os.path.exists(p): return []
    print "Loading data from '{0}'".format(p)
    fp = gzip.open(p, 'rb')
    data = cPickle.load(fp)
    fp.close()
    return data
        
# helper - parse shadow timestamps
def parsetimestamp(stamp):
    parts = stamp.split(":")
    h, m, s, ns = int(parts[0]), int(parts[1]), int(parts[2]), int(parts[3])
    seconds = float(h*3600.0) + float(m*60.0) + float(s) + float(ns/1000000000.0)
    return seconds

# helper - parse value from a list of CODE=VALUE type strings
def parsecode(parts, code):
    for part in parts:
        if part.find(code)>-1: return part.split('=')[1]
    return None

# helper - fixup any missing ticks in data-over-time collections
def fixupticks(data):
    if len(data.keys()) > 0:
        for tick in range(0, int(max(data.keys()))+1):
            if tick not in data: data[tick] = 0.0
    return data

# helper - parse TIME_CREATED from CIRC event
def parsecreatetime(parts):
    createtimestamp = parsecode(parts, "TIME_CREATED").split('T')[1]
    timeparts = createtimestamp.split(':')
    createtime = int(timeparts[0])*3600.0 + int(timeparts[1])*60.0 + float(timeparts[2])
    return createtime

## helper - cumulative fraction for y axis
def cf(d): return pylab.arange(1.0,float(len(d))+1.0)/float(len(d))

## helper - return step-based CDF x and y values
## only show to the 99th percentile by default
def getcdf(data, shownpercentile=0.99):
    data.sort()
    frac = cf(data)
    x, y, lasty = [], [], 0.0
    for i in xrange(int(round(len(data)*shownpercentile))):
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

