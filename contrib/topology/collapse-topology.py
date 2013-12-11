#!/usr/bin/python

import sys
import networkx as nx

INPUT_GRAPH="topology.complete.graphml.xml"
OUTPUT_GRAPH="topology.graphml.xml"

def copy_edge_props(srce, dste):
    for attr in srce:
        if attr not in dste: dste[attr] = []
        dste[attr].append(float(srce[attr]))

def copy_node_props(srcn, dstn):
    if 'type' not in dstn:
        for attr in srcn: dstn[attr] = srcn[attr]
        dstn['type'] = 'cluster'
        dstn['asn'] = '0'

def main():
    print "reading graph..."
    G = nx.read_graphml(INPUT_GRAPH)

    print "collapsing graph..."
    Gnew = nx.Graph()
    for (srcid, dstid) in G.edges():
        e = G.edge[srcid][dstid]
        srcn, dstn = G.node[srcid], G.node[dstid]
        if 'geocode' in srcn and 'geocode' in dstn:
            srcg, dstg = srcn['geocode'], dstn['geocode']
            Gnew.add_edge(srcg, dstg)
            copy_edge_props(e, Gnew.edge[srcg][dstg])
            copy_node_props(srcn, Gnew.node[srcg])
            copy_node_props(dstn, Gnew.node[dstg])

    print "adjusting properties..."
    for attr in G.graph: Gnew.graph[attr] = G.graph[attr]
    for (srcid, dstid) in Gnew.edges():
        e = Gnew.edge[srcid][dstid]
        for attr in e: e[attr] = "{0}".format(float(sum(e[attr])) / float(len(e[attr])))

    print "checking graph..."
    assert nx.is_connected(Gnew)
    assert nx.number_connected_components(Gnew) == 1

    print "writing graph..."
    nx.write_graphml(Gnew, OUTPUT_GRAPH)

    print "done!"

if __name__ == '__main__': main()
