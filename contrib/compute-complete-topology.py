#!/usr/bin/python

import networkx as nx

G = nx.read_graphml("topology.full.graphml.xml")

print "read graph ok"

for (srcid, dstid) in G.edges():
    e = G.edge[srcid][dstid]
    l = e['latencies'].split(',')
    w = float(l[5]) if len(l) == 10 else float(l[0])
    e['weight'] = w

print "get weights ok"

d = nx.all_pairs_dijkstra_path_length(G)

print "get distances ok"

G.remove_edges_from(G.edges())

print "removed old edges ok"

for i in G:
    for j in G:
        G.add_edge(i, j, latencies=d[i][j])

print "added new edges ok"

nx.write_graphml(G, "topology.complete.graphml.xml")

print "write graph ok"

