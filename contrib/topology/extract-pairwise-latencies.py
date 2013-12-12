#!/usr/bin/python

import csv, networkx as nx

INPUT_GRAPH="topology.graphml.xml"
OUTPUT_CSV="pairwise_latencies.csv"

def main():
    print "reading graph..."
    G = nx.read_graphml(INPUT_GRAPH)

    print "writing csv..."
    nodes = sorted(G.nodes())
    with open(OUTPUT_CSV, 'wb') as f:
        w = csv.writer(f)
        header = [''] + [G.node[nid]['geocode'] for nid in nodes]
        w.writerow(header)
        for nid1 in nodes:
            row = [G.node[nid1]['geocode']]
            for nid2 in nodes:
                row.append(G.edge[nid1][nid2]['latency'])
            w.writerow(row)

    print "done!"

if __name__ == '__main__': main()
