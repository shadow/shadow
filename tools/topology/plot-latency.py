#!/usr/bin/python

import networkx as nx
import matplotlib; matplotlib.use('Agg') # for systems without X11
import sys, os, pylab

if len(sys.argv) != 2: print >>sys.stderr, "{0} graphml.xml".format(sys.argv[0]);exit()
GRAPH=sys.argv[1]

def main():
    print "reading graph..."
    G = nx.read_graphml(GRAPH)

    print "gathering latencies..."
    l = [G.edge[s][d]['latency'] for (s, d) in G.edges()]
    x, y = getcdf(l)

    print "plotting latencies..."
    pylab.figure()
    pylab.plot(x, y, label="edge latency", c='k', ls='-')
    pylab.xscale('log')
    pylab.title("Edge Latency for graph file '{0}'".format(os.path.basename(GRAPH)))
    pylab.xlabel("Time (ms)")
    pylab.ylabel("Cumulative Fraction")
    #pylab.legend(loc="lower right")

    outname = "edge-latency.cdf.pdf"
    print "saving pdf image '{0}'".format(outname)
    pylab.savefig(outname)

## cumulative fraction for y axis
def cf(d): return pylab.arange(1.0,float(len(d))+1.0)/float(len(d))

## return step-based CDF x and y values
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

if __name__ == '__main__': main()
