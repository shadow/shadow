#!/usr/bin/python

import networkx as nx

INGRAPH="tgen.webclient.graphml.xml"
OUTGRAPH="tgen.edited.webclient.graphml.xml"
LOGLEVEL="debug"

def main():
    print "reading graph..."
    G = nx.read_graphml(INGRAPH)

    print "editing loglevel attribute..."
    for n in G.nodes():
        if n == 'start':
            G.node[n]['loglevel'] = LOGLEVEL

    print "writing graph..."
    nx.write_graphml(G, OUTGRAPH)

if __name__ == '__main__': main()
