#!/usr/bin/python

import networkx as nx
from random import sample

G = nx.read_graphml("topology.full.graphml.xml")

print "read graph ok"

G.remove_edges_from(G.edges())

print "removed old edges ok"

relays, servers, pops = set(), set(), set()
for nid in G.nodes():
    n = G.node[nid]
    if 'nodetype' not in n: continue
    if n['nodetype'] == 'pop': pops.add(nid)
    elif n['nodetype'] == 'relay': relays.add(nid)
    elif n['nodetype'] == 'server': servers.add(nid)

keep = sample(pops, 20) + sample(relays, 20) + sample(servers, 20)
remove = list(set(G.nodes()).difference(keep))
G.remove_nodes_from(remove)

print "{0} node left".format(len(G.nodes()))

for i in G:
    for j in G:
        G.add_edge(i, j, latencies=str(30))

print "added new edges ok"

nx.write_graphml(G, "topology.test.graphml.xml")

print "write graph ok"

