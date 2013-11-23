#!/usr/bin/python

import time, sys
import networkx as nx
from random import sample
from multiprocessing import Process, JoinableQueue
from threading import Thread

def manager(taskq, resultq, finaldistq, logq, pois):
    d = {}
    npois = len(pois)
    start = time.time()
    count = 0

    while not taskq.empty():
        result = resultq.get()
        srcid, dists = result[0], result[1]
        d[srcid] = dists
        count += 1
        est = (((time.time() - start) / count) * (npois - count)) / 3600.0
        msg = "\rfinished {0}/{1}, estimated hours remaining: {2}".format(count, npois, est)
        logq.put(msg)
        #sys.stdout.write(msg)
        #sys.stdout.flush()

    finaldistq.put(d)

def worker(taskq, resultq, G):
    while not taskq.empty():
        srcid = taskq.get()
        dists = nx.single_source_dijkstra_path_length(G, srcid)
        resultq.put((srcid, dists))
        taskq.task_done()

def distribute(num_processes, G, pois):
    taskq = JoinableQueue()
    resultq = JoinableQueue()
    finaldistq = JoinableQueue()
    logq = JoinableQueue()

    for i in pois: taskq.put(i)

    m = Process(target=manager, args=(taskq, resultq, finaldistq, logq, pois))

    workers = []
    for i in range(num_processes):
        p = Process(target=worker, args=(taskq, resultq, G))
        p.start()
        workers.append(p)

    while True:
        msg = logq.get()
        sys.stdout.write(msg)
        sys.stdout.flush()
        if taskq.empty(): break

    taskq.join()
    for p in workers: p.join()

    d = finaldistq.get()
    m.join()

    return d

def main():
    if len(sys.argv) != 2: print "usage: {0} num_processes".format(sys.argv[0]); exit()
    num_processes = int(sys.argv[1])

    # get the original graph
    print "reading graph..."

    G = nx.read_graphml("topology.full.graphml.xml")

    # extract the node geocodes and the set of nodes with pop-type as client poi candidates
    print "extracting candidate clients..."

    relays, servers, clients, pops = set(), set(), set(), set()
    codes = dict()
    nodes = G.nodes()
    for nid in nodes:
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

    for nid in sample(pops, 10000):
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
    G.remove_nodes_from(list(pops))

    print "added new edges..."

    for srcid in pois:
        for dstid in pois:
            G.add_edge(srcid, dstid, latencies=d[srcid][dstid])

    print "write graph..."

    nx.write_graphml(G, "topology.trimmed.graphml.xml")

if __name__ == '__main__': main()

