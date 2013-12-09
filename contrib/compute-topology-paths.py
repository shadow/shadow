#!/usr/bin/python

import time, sys
import networkx as nx
from random import sample
from multiprocessing import Process, Queue, cpu_count
from threading import Thread, Lock

INPUT_GRAPH="topology.full.graphml.xml"
OUTPUT_GRAPH="topology.complete.graphml.xml"
CLIENT_SAMPLE_SIZE=10000

def worker(taskq, resultq, G, pois):
    for srcid in iter(taskq.get, 'STOP'):
        dists = nx.single_source_dijkstra_path_length(G, srcid)
        d = {}
        for dstid in dists: 
            if dstid in pois: d[dstid] = dists[dstid]
        resultq.put((srcid, d))

def thread(resultq, Gnew, glock, doneq):
    for r in iter(resultq.get, 'STOP'):
        srcid, d = r[0], r[1]
        glock.acquire()
        for dstid in d: Gnew.add_edge(srcid, dstid, latencies=d[dstid])
        glock.release()
        doneq.put(True)

def run_distributed(num_processes, G, pois, Gnew):
    # each task is to do a single source shortest path for a node
    taskq = Queue()
    for i in pois: taskq.put(i)

    glock = Lock()

    # start the worker processes
    workers = []
    doneq = Queue()
    for i in range(num_processes):
        resultq = Queue()
        p = Process(target=worker, args=(taskq, resultq, G, pois))
        t = Thread(target=thread, args=(resultq, Gnew, glock, doneq))
        t.setDaemon(True)
        workers.append([p, t, resultq])
    for w in workers: w[0].start();w[1].start()

    # we will collect the distances from the workers
    npois = len(pois)
    start = time.time()
    count = 0
    while count < npois:
        result = doneq.get()
#        srcid, dists = result[0], result[1]
#        d[srcid] = dists
        count += 1
        est = (((time.time() - start) / count) * (npois - count)) / 3600.0
        msg = "\rfinished {0}/{1}, {3} waiting, estimated hours remaining: {2}".format(count, npois, est, doneq.qsize())
        sys.stdout.write(msg)
        sys.stdout.flush()

    # tell workesr to stop
    for w in workers:
        taskq.put('STOP')
        w[2].put('STOP')
#        w[0].join()
#        w[1].join()

    return Gnew

def run_single(G, pois, Gnew):
    d = nx.all_pairs_dijkstra_path_length(G)
    for srcid in pois:
        for dstid in pois:
            Gnew.add_edge(srcid, dstid, latencies=d[srcid][dstid])
    return Gnew

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

    print "creating destination graph, removing old edges and nodes..."

    pois = clients.union(servers.union(relays))
    Gnew = G.copy()
    Gnew.remove_edges_from(G.edges())
    Gnew.remove_nodes_from(pops)

    print "getting dijkstra shortest path lengths..."

    Gnew = run_distributed(num_processes, G, pois, Gnew)
    #Gnew = run_single(G, pois, Gnew)

    print ""

    print "checking graph..."

    # handle the stupid dummy node
    for srcid in others:
        for dstid in pois:
            G.add_edge(srcid, dstid, latencies=10000)

    assert nx.is_connected(G)
    assert nx.number_connected_components(G) == 1

    print "writing graph..."

    nx.write_graphml(G, OUTPUT_GRAPH)

    print "done!"

if __name__ == '__main__': main()

