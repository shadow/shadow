#!/usr/bin/python

import networkx as nx

GRAPH="topology.complete.graphml.xml"

G = nx.read_graphml(GRAPH)

for (s, d) in G.edges():
    if float(G.edge[s][d]['latency']) == 0.0:
        G.edge[s][d]['latency'] = 5.0

nx.write_graphml(G, GRAPH)

