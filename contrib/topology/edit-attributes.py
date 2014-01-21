#!/usr/bin/python

import networkx as nx

INGRAPH="topology.graphml.xml"
OUTGRAPH="topology.edited.graphml.xml"

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

def set_node_packet_loss(G, val):
    for n in G.nodes():
        if 'packetloss' in G.node[n]:
            G.node[n]['packetloss'] = val
    return G

def set_edge_packet_loss(G, val):
    for (s, d) in G.edges():
        if 'packetloss' in G.edge[s][d]:
            G.edge[s][d]['packetloss'] = val
    return G

def main():
    print "reading graph..."
    G = nx.read_graphml(INGRAPH)

    print "editing attributes..."
#    G = ensure_nonzero_latency(G)
    G = set_node_packet_loss(G, 0.0)
    G = set_edge_packet_loss(G, 0.005)

    print "writing graph..."
    nx.write_graphml(G, OUTGRAPH)

if __name__ == '__main__': main()
