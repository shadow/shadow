#!/usr/bin/python

import networkx as nx
from numpy import median

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
        dstn['asn'] = int(0)

def main():
    print "reading graph..."
    G = nx.read_graphml(INPUT_GRAPH)

    print "collapsing graph..."
    Gnew = nx.Graph()
    geoid = {}
    poicounter = 0
    for (srcid, dstid) in G.edges():
        e = G.edge[srcid][dstid]
        srcn, dstn = G.node[srcid], G.node[dstid]
        if 'geocode' in srcn and 'geocode' in dstn:
            srcg, dstg = srcn['geocode'], dstn['geocode']
            if srcg not in geoid:
                poicounter += 1
                geoid[srcg] = "poi-{0}".format(poicounter)
            if dstg not in geoid:
                poicounter += 1
                geoid[dstg] = "poi-{0}".format(poicounter)
            Gnew.add_edge(geoid[srcg], geoid[dstg])
            copy_edge_props(e, Gnew.edge[geoid[srcg]][geoid[dstg]])
            copy_node_props(srcn, Gnew.node[geoid[srcg]])
            copy_node_props(dstn, Gnew.node[geoid[dstg]])

    print "adjusting properties..."
    for attr in G.graph: Gnew.graph[attr] = G.graph[attr]
    for (srcid, dstid) in Gnew.edges():
        e = Gnew.edge[srcid][dstid]
        for attr in e: e[attr] = float(median(e[attr]))
        if e['latency'] <= 0.0: print "warning: found 0 latency between {0} and {1}, please run edit-attributes.py on the resulting graph file".format(srcid, dstid)

    print "checking graph..."
    assert nx.is_connected(Gnew)
    assert nx.number_connected_components(Gnew) == 1

    print "writing graph..."
    nx.write_graphml(Gnew, OUTPUT_GRAPH)

    print "done!"

if __name__ == '__main__': main()
