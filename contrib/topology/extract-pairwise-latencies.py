#!/usr/bin/python

import sys, csv
import networkx as nx
from numpy import mean, median

INPUT_GRAPH="topology.graphml.xml"
OUTPUT_CSV="pairwise_latencies.csv"

def main():
    print "reading graph..."
    G = nx.read_graphml(INPUT_GRAPH)

    print "writing csv..."
    nodes = sorted(G.nodes())
    with open(OUTPUT_CSV, 'wb') as f:
        w = csv.writer(f)
        w.writerow([''] + nodes)
        for nid1 in nodes:
            row = [nid1]
            for nid2 in nodes:
                lstr = G.edge[nid1][nid2]['latency']
                l = [float(i) for i in lstr.split(',')]
                row.append(str(median(l)))
            w.writerow(row)

if __name__ == '__main__': main()
