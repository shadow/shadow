#!/usr/bin/python

import matplotlib; matplotlib.use('Agg') # for systems without X11
from matplotlib.backends.backend_pdf import PdfPages
import sys, os, argparse, subprocess, json, pylab, numpy
from itertools import cycle

"""
python parse-shadow.py --help
"""

pylab.rcParams.update({
    'backend': 'PDF',
    'font.size': 16,
    'figure.figsize': (6,4.5),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.15,
    'figure.subplot.right': 0.95,
    'figure.subplot.bottom': 0.15,
    'figure.subplot.top': 0.95,
    'grid.color': '0.1',
    'axes.grid' : True,
    'axes.titlesize' : 'small',
    'axes.labelsize' : 'small',
    'axes.formatter.limits': (-4,4),
    'xtick.labelsize' : 'small',
    'ytick.labelsize' : 'small',
    'lines.linewidth' : 2.0,
    'lines.markeredgewidth' : 0.5,
    'lines.markersize' : 10,
    'legend.fontsize' : 'x-small',
    'legend.fancybox' : False,
    'legend.shadow' : False,
    'legend.ncol' : 1.0,
    'legend.borderaxespad' : 0.5,
    'legend.numpoints' : 1,
    'legend.handletextpad' : 0.5,
    'legend.handlelength' : 1.6,
    'legend.labelspacing' : .75,
    'legend.markerscale' : 1.0,
    'ps.useafm' : True,
    'pdf.use14corefonts' : True,
    'text.usetex' : True,
})

LINEFORMATS="k-,r-,b-,g-,c-,m-,y-,k--,r--,b--,g--,c--,m--,y--,k:,r:,b:,g:,c:,m:,y:,k-.,r-.,b-.,g-.,c-., m-.,y-."

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
        dest.append((datapath, label))

def main():
    parser = argparse.ArgumentParser(
        description='Utility to help plot results from the Shadow simulator', 
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument('-d', '--data', 
        help="""Append a PATH to the directory containing the xz-compressed
                tor.throughput.json.xz and filetransfer.downloads.json.xz,
                and the LABEL we should use for the graph legend for this
                set of experimental results""", 
        metavar=("PATH", "LABEL"),
        nargs=2,
        required="True",
        action=PlotDataAction, dest="experiments")

    parser.add_argument('-p', '--prefix', 
        help="a STRING filename prefix for graphs we generate", 
        metavar="STRING",
        action="store", dest="prefix",
        default=None)

    parser.add_argument('-f', '--format',
        help="""A comma-separated LIST of color/line format strings to cycle to 
                matplotlib's plot command (see matplotlib.pyplot.plot)""", 
        metavar="LIST",
        action="store", dest="lineformats",
        default=LINEFORMATS)

    args = parser.parse_args()
    shdata, ftdata, tordata = get_data(args.experiments, args.lineformats)

    page = PdfPages("{0}shadowtor.pdf".format(args.prefix+'.' if args.prefix is not None else ''))
    plot_shadow(shdata, page, "recv")
    plot_shadow(shdata, page, "send")
    plot_filetransfer_firstbyte(ftdata, page)
    plot_filetransfer_lastbyte(ftdata, page)
    plot_filetransfer_downloads(ftdata, page)
    plot_tor(tordata, page)
    page.close()

def plot_shadow(datasource, page, direction):
    total_mafig, total_aggcdffig, total_percdffig = pylab.figure(), pylab.figure(), pylab.figure()
    data_mafig, data_aggcdffig, data_percdffig = pylab.figure(), pylab.figure(), pylab.figure()
    control_mafig, control_aggcdffig, control_percdffig = pylab.figure(), pylab.figure(), pylab.figure()
    retrans_mafig, retrans_aggcdffig, retrans_percdffig = pylab.figure(), pylab.figure(), pylab.figure()

    for (d, label, lineformat) in datasource:
        total, data, control, retrans = {}, {}, {}, {}
        totalper, datafper, controlfper, retransfper = [], [], [], []
        for node in d:
            for t in d[node][direction]['bytes_total']:
                if t not in total: total[t] = 0
                if t not in data: data[t] = 0
                if t not in control: control[t] = 0
                if t not in retrans: retrans[t] = 0

                total[t] += d[node][direction]['bytes_total'][t]/1048576.0
                data[t] += d[node][direction]['bytes_data'][t]/1048576.0
                control[t] += d[node][direction]['bytes_control'][t]/1048576.0
                retrans[t] += d[node][direction]['bytes_retrans'][t]/1048576.0

                v = float(d[node][direction]['bytes_total'][t]/1048576.0)
                totalper.append(v)
                datafper.append(v if v == 0.0 else d[node][direction]['bytes_data'][t]/v)
                controlfper.append(v if v == 0.0 else d[node][direction]['bytes_control'][t]/v)
                retransfper.append(v if v == 0.0 else d[node][direction]['bytes_retrans'][t]/v)

        pylab.figure(total_mafig.number)
        x = sorted(total.keys())
        y = [total[t] for t in x]
        y_ma = movingaverage(y, 60)
        #pylab.scatter(x, y, s=0.1)
        pylab.plot(x, y_ma, lineformat, label=label)
        
        pylab.figure(total_aggcdffig.number)
        x, y = getcdf(y)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(total_percdffig.number)
        x, y = getcdf(totalper)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(data_mafig.number)
        x = sorted(data.keys())
        y = [data[t] for t in x]
        y_ma = movingaverage(y, 60)
        #pylab.scatter(x, y, s=0.1)
        pylab.plot(x, y_ma, lineformat, label=label)
        
        pylab.figure(data_aggcdffig.number)
        x, y = getcdf(y)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(data_percdffig.number)
        x, y = getcdf(datafper)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(control_mafig.number)
        x = sorted(control.keys())
        y = [control[t] for t in x]
        y_ma = movingaverage(y, 60)
        #pylab.scatter(x, y, s=0.1)
        pylab.plot(x, y_ma, lineformat, label=label)
        
        pylab.figure(control_aggcdffig.number)
        x, y = getcdf(y)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(control_percdffig.number)
        x, y = getcdf(controlfper)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(retrans_mafig.number)
        x = sorted(retrans.keys())
        y = [retrans[t] for t in x]
        y_ma = movingaverage(y, 60)
        #pylab.scatter(x, y, s=0.1)
        pylab.plot(x, y_ma, lineformat, label=label)
        
        pylab.figure(retrans_aggcdffig.number)
        x, y = getcdf(y)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(retrans_percdffig.number)
        x, y = getcdf(retransfper)
        pylab.plot(x, y, lineformat, label=label)

    pylab.figure(total_mafig.number)
    pylab.xlabel("Tick (s)")
    pylab.ylabel("Throughput (MiB/s)")
    pylab.title("60 second moving average {0} throughput, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(total_aggcdffig.number)
    pylab.xlabel("Throughput (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second {0} throughput, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(total_percdffig.number)
    #pylab.xscale('log')
    pylab.xlabel("Throughput (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second {0} throughput, per node".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(data_mafig.number)
    pylab.xlabel("Tick (s)")
    pylab.ylabel("Goodput (MiB/s)")
    pylab.title("60 second moving average {0} goodput, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(data_aggcdffig.number)
    pylab.xlabel("Goodput (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second {0} goodput, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(data_percdffig.number)
    #pylab.xscale('log')
    pylab.xlabel("Goodput / Throughput")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second fractional {0} goodput, per node".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(control_mafig.number)
    pylab.xlabel("Tick (s)")
    pylab.ylabel("Control Overhead (MiB/s)")
    pylab.title("60 second moving average {0} control overhead, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(control_aggcdffig.number)
    pylab.xlabel("Control Overhead (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second {0} control overhead, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(control_percdffig.number)
    #pylab.xscale('log')
    pylab.xlabel("Control Overhead / Throughput")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second fractional {0} control overhead, per node".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(retrans_mafig.number)
    pylab.xlabel("Tick (s)")
    pylab.ylabel("Retransmission Overhead (MiB/s)")
    pylab.title("60 second moving average {0} retrans overhead, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(retrans_aggcdffig.number)
    pylab.xlabel("Retransmission Overhead (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second {0} retrans overhead, all nodes".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(retrans_percdffig.number)
    #pylab.xscale('log')
    pylab.xlabel("Retransmission Overhead / Throughput")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second fractional {0} retrans overhead, per node".format(direction))
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

def plot_filetransfer_firstbyte(data, page):
    pylab.figure()
    
    for (d, label, lineformat) in data:
        fb = []
        for client in d:
            for bytes in d[client]:
                client_fb_list = d[client][bytes]["firstbyte"]
                for sec in client_fb_list: fb.append(sec)
        x, y = getcdf(fb)
        pylab.plot(x, y, lineformat, label=label)

    pylab.xlabel("Download Time (s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("time to download first byte, all clients")
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

def plot_filetransfer_lastbyte(data, page):
    figs = {}
    
    for (d, label, lineformat) in data:
        lb = {}
        for client in d:
            for bytes in d[client]:
                if bytes not in figs: figs[bytes] = pylab.figure()
                if bytes not in lb: lb[bytes] = []
                client_lb_list = d[client][bytes]["lastbyte"]
                for sec in client_lb_list: lb[bytes].append(sec)
        for bytes in lb:
            x, y = getcdf(lb[bytes])
            pylab.figure(figs[bytes].number)
            pylab.plot(x, y, lineformat, label=label)

    for bytes in figs:
        pylab.figure(figs[bytes].number)
        pylab.xlabel("Download Time (s)")
        pylab.ylabel("Cumulative Fraction")
        pylab.title("time to download {0} bytes, all clients".format(bytes))
        pylab.legend(loc="lower right")
        page.savefig()
        pylab.close()

def plot_filetransfer_downloads(data, page):
    figs = {}
    
    for (d, label, lineformat) in data:
        dls = {}
        for client in d:
            for bytes in d[client]:
                if bytes not in figs: figs[bytes] = pylab.figure()
                if bytes not in dls: dls[bytes] = {}
                if client not in dls[bytes]: dls[bytes][client] = 0
                client_lb_list = d[client][bytes]["lastbyte"]
                for sec in client_lb_list: dls[bytes][client] += 1
        for bytes in dls:
            x, y = getcdf(dls[bytes].values(), shownpercentile=1.0)
            pylab.figure(figs[bytes].number)
            pylab.plot(x, y, lineformat, label=label)

    for bytes in figs:
        pylab.figure(figs[bytes].number)
        pylab.xlabel("Downloads Completed (\#)")
        pylab.ylabel("Cumulative Fraction")
        pylab.title("number of {0} byte downloads completed, per client".format(bytes))
        pylab.legend(loc="lower right")
        page.savefig()
        pylab.close()

def plot_tor(data, page):
    mafig = pylab.figure()
    aggcdffig = pylab.figure()
    percdffig = pylab.figure()

    for (d, label, lineformat) in data:
        tput = {}
        pertput = []
        for node in d:
            if 'relay' not in node and 'thority' not in node: continue
            for t in d[node]["bytes_written"]:
                if t not in tput: tput[t] = 0
                mib = d[node]["bytes_written"][t]/1048576.0
                tput[t] += mib
                pertput.append(mib)

        pylab.figure(mafig.number)
        x = sorted(tput.keys())
        y = [tput[t] for t in x]
        y_ma = movingaverage(y, 60)
        #pylab.scatter(x, y, s=0.1)
        pylab.plot(x, y_ma, lineformat, label=label)
        
        pylab.figure(aggcdffig.number)
        x, y = getcdf(y)
        pylab.plot(x, y, lineformat, label=label)

        pylab.figure(percdffig.number)
        x, y = getcdf(pertput)
        pylab.plot(x, y, lineformat, label=label)

    pylab.figure(mafig.number)
    pylab.xlabel("Tick (s)")
    pylab.ylabel("Throughput (MiB/s)")
    pylab.title("60 second moving average throughput, all relays")
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(aggcdffig.number)
    pylab.xlabel("Throughput (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second throughput, all relays")
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

    pylab.figure(percdffig.number)
    #pylab.xscale('log')
    pylab.xlabel("Throughput (MiB/s)")
    pylab.ylabel("Cumulative Fraction")
    pylab.title("per second throughput, per relay")
    pylab.legend(loc="lower right")
    page.savefig()
    pylab.close()

def get_data(experiments, lineformats):
    shdata, ftdata, tordata = [], [], []
    lflist = lineformats.strip().split(",")

    lfcycle = cycle(lflist)
    for (path, label) in experiments:
        log = os.path.abspath(os.path.expanduser("{0}/shadow.packets.json.xz".format(path)))
        xzcatp = subprocess.Popen(["xzcat", log], stdout=subprocess.PIPE)
        data = json.load(xzcatp.stdout)
        shdata.append((data, label, lfcycle.next()))

    lfcycle = cycle(lflist)
    for (path, label) in experiments:
        log = os.path.abspath(os.path.expanduser("{0}/filetransfer.downloads.json.xz".format(path)))
        xzcatp = subprocess.Popen(["xzcat", log], stdout=subprocess.PIPE)
        data = json.load(xzcatp.stdout)
        ftdata.append((data, label, lfcycle.next()))

    lfcycle = cycle(lflist)
    for (path, label) in experiments:
        log = os.path.abspath(os.path.expanduser("{0}/tor.throughput.json.xz".format(path)))
        xzcatp = subprocess.Popen(["xzcat", log], stdout=subprocess.PIPE)
        data = json.load(xzcatp.stdout)
        tordata.append((data, label, lfcycle.next()))

    return shdata, ftdata, tordata

# helper - compute the window_size moving average over the data in interval
def movingaverage(interval, window_size):
    window = numpy.ones(int(window_size))/float(window_size)
    return numpy.convolve(interval, window, 'same')

## helper - cumulative fraction for y axis
def cf(d): return pylab.arange(1.0,float(len(d))+1.0)/float(len(d))

## helper - return step-based CDF x and y values
## only show to the 99th percentile by default
def getcdf(data, shownpercentile=0.99):
    data.sort()
    frac = cf(data)
    x, y, lasty = [], [], 0.0
    for i in xrange(int(round(len(data)*shownpercentile))):
        assert not numpy.isnan(data[i])
        x.append(data[i])
        y.append(lasty)
        x.append(data[i])
        y.append(frac[i])
        lasty = frac[i]
    return x, y

if __name__ == '__main__': sys.exit(main())
