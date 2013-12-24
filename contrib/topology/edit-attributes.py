#!/usr/bin/python

import networkx as nx

GRAPH="topology.graphml.xml"

def ensure_nonzero_latency(G):
    lintra, linter, zeros = [], [], []

    for (s, d) in G.edges():
        l = G.edge[s][d]['latency']
        if l <= 0.0: zeros.append((s,d))
        elif s == d: lintra.append(l)
        else: linter.append(l)

    lintramean = float(sum(lintra))/float(len(lintra))
    lintermean = float(sum(linter))/float(len(linter))

    for (s, d) in zeros:
        if s == d: G.edge[s][d]['latency'] = lintramean
        else: G.edge[s][d]['latency'] = lintermean

    return G

def main():
    print "reading graph..."
    G = nx.read_graphml(GRAPH)

    print "editing attributes..."
    G = ensure_nonzero_latency(G)
#    for (s, d) in G.edges():
#        if float(G.edge[s][d]['latency']) == 0.0:
#            G.edge[s][d]['latency'] = 5.0

    print "writing graph..."
    nx.write_graphml(G, GRAPH)

if __name__ == '__main__': main()
