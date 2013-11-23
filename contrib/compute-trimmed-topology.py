#!/usr/bin/python

import time, sys
import networkx as nx
from random import sample
from multiprocessing import Process, Queue, cpu_count
from threading import Thread

INPUT_GRAPH="topology.test.graphml.xml"
OUTPUT_GRAPH="topology.trimmed.graphml.xml"
CLIENT_SAMPLE_SIZE=10000

def worker(taskq, resultq, G):
    for srcid in iter(taskq.get, 'STOP'):
        dists = nx.single_source_dijkstra_path_length(G, srcid)
        resultq.put((srcid, dists))

def distribute(num_processes, G, pois):
    taskq, resultq = Queue(), Queue()

    # each task is to do a single source shortest path for a node
    for i in pois:
        taskq.put(i)

    # start the worker processes
    for i in range(num_processes):
        Process(target=worker, args=(taskq, resultq, G)).start()

    # we will collect the distances from the workers
    d = {}
    npois = len(pois)
    start = time.time()
    count = 0
    while count < npois:
        result = resultq.get()
        srcid, dists = result[0], result[1]
        d[srcid] = dists
        count += 1
        est = (((time.time() - start) / count) * (npois - count)) / 3600.0
        msg = "\rfinished {0}/{1}, estimated hours remaining: {2}".format(count, npois, est)
        sys.stdout.write(msg)
        sys.stdout.flush()

    # tell workesr to stop
    for i in range(num_processes):
        taskq.put('STOP')

    return d

def main():
    num_processes = cpu_count()
    if len(sys.argv) > 1:
        num_processes = int(sys.argv[1])

    # get the original graph
    print "reading graph..."

    G = nx.read_graphml(INPUT_GRAPH)

    # extract the node geocodes and the set of nodes with pop-type as client poi candidates
    print "extracting candidate clients..."

    relays, servers, clients, pops, others = set(), set(), set(), set(), set()
    codes = dict()
    nodes = G.nodes()
    for nid in nodes:
        if nid == 'dummynode':
            others.add(nid)
            continue
        n = G.node[nid]
        if 'nodetype' in n:
            if n['nodetype'] == 'pop':
                pops.add(nid)
                if 'geocodes' in n:
                    code = n['geocodes'].split(',')[0]
                    if code not in codes: codes[code] = nid
            elif n['nodetype'] == 'relay': relays.add(nid)
            elif n['nodetype'] == 'server': servers.add(nid)

    # sample the pops and make sure we have at least one from each geocode
    print "selecting clients..."

    for nid in sample(pops, CLIENT_SAMPLE_SIZE):
        clients.add(nid)
        n = G.node[nid]
        if 'geocodes' in n:
            code = n['geocodes'].split(',')[0]
            if code in codes: del(codes[code])
    for nid in codes.values(): clients.add(nid)

    # choose the median latency to act as the edge weight for all edges
    print "get weights..."

    for (srcid, dstid) in G.edges():
        e = G.edge[srcid][dstid]
        l = e['latencies'].split(',')
        w = (float(l[4])+float(l[5]))/2.0 if len(l) == 10 else float(l[0])
        e['weight'] = w

    print "get dijkstra shortest path lengths..."

    pois = clients.union(servers.union(relays))
    d = distribute(num_processes, G, pois)

    print ""
    print "removed old edges and nodes..."

    G.remove_edges_from(G.edges())
    G.remove_nodes_from(set(nodes).difference(pois.union(others)))

    print "added new edges..."

    for srcid in pois:
        for dstid in pois:
            G.add_edge(srcid, dstid, latencies=d[srcid][dstid])

    # handle the stupid dummy node
    for srcid in others:
        for dstid in pois:
            G.add_edge(srcid, dstid, latencies=10000)

    assert nx.is_connected(G)
    assert nx.number_connected_components(G) == 1

    print "write graph..."

    nx.write_graphml(G, OUTPUT_GRAPH)

if __name__ == '__main__': main()

