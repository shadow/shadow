#!/usr/bin/python

import sys, os
import networkx as nx

INPUT_GRAPH="topology.full.graphml.xml"
OUTPUT_GRAPH="topology.pruned.graphml.xml"

def main():
    if len(sys.argv) != 2: print "USAGE: {0} path/to/consensus/file".format(sys.argv[0]);exit()
    consensus_filename = os.path.abspath(os.path.expanduser(sys.argv[1]))

    print "reading graph..."
    G = nx.read_graphml(INPUT_GRAPH)

    print "getting relays..."
    relays =  dict()
    for nid in G.nodes():
        if nid == 'dummynode': continue
        n = G.node[nid]
        if 'nodetype' in n and n['nodetype'] == 'relay': relays[nid] = ip2long(nid)

    print "graph has {0} relays".format(len(relays))
    print "matching relays from consensus at {0}...".format(consensus_filename)

    keep = set()
    num = 0
    with open(consensus_filename, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if parts[0] == 'r':
                num += 1
                ip = parts[6]
                keep.add(getbestmatch(relays, ip))
                sys.stdout.write("\rmatched {0} relay ips".format(num))
                sys.stdout.flush()

    print ""
    print "{0} relays match in graph".format(len(keep))

    for nid in relays:
        if nid not in keep: G.remove_node(nid)

    print "checking graph..."

    assert nx.is_connected(G)
    assert nx.number_connected_components(G) == 1

    print "writing graph..."

    nx.write_graphml(G, OUTPUT_GRAPH)

    print "done!"

def getbestmatch(relays, ip):
    bestip = None
    bestmatch = 0
    ipnum = ip2long(ip)
    for rip in relays:
        ripnum = relays[rip]
        match = ipnum&ripnum
        if match > bestmatch: bestmatch = match; bestip = rip
    assert bestip != None
    return bestip

def ip2long(ip):
    """
    Convert a IPv4 address into a 32-bit integer.
    
    @param ip: quad-dotted IPv4 address
    @type ip: str
    @return: network byte order 32-bit integer
    @rtype: int
    """
    ip_array = ip.split('.')
    ip_long = long(ip_array[0]) * 16777216 + long(ip_array[1]) * 65536 + long(ip_array[2]) * 256 + long(ip_array[3])
    return ip_long

if __name__ == '__main__': main()

