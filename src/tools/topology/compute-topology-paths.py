#!/usr/bin/python

import time, sys
import networkx as nx
from random import sample
from multiprocessing import Process, Queue, cpu_count
from threading import Thread, Lock

INPUT_GRAPH="topology.pruned.graphml.xml"
OUTPUT_GRAPH="topology.complete.graphml.xml"
CLIENT_SAMPLE_SIZE=10000

def worker(taskq, resultq, G, pois):
    for srcid in iter(taskq.get, 'STOP'):
#        dists = nx.single_source_dijkstra_path_length(G, srcid)
#        d = {}
#        for dstid in dists: 
#            if dstid in pois: d[dstid] = dists[dstid]

        path = nx.single_source_dijkstra_path(G, srcid)
        d = {}
        for dstid in path:
            if dstid not in pois: continue
            l, j = [], []
            p = path[dstid]
            if len(p) <= 1:
                l.append(5.0)
                j.append(0.0)
            else:
                for i in xrange(len(p)-1):
                    e = G.edge[p[i]][p[i+1]]
                    l.append(float(e['latency']))
                    j.append(float(e['jitter']))
            d[dstid] = {'latency':float(sum(l)), 'jitter':float(sum(j)/float(len(j)))}

        resultq.put((srcid, d))

def thread(resultq, Gnew, glock, doneq):
    for r in iter(resultq.get, 'STOP'):
        srcid, d = r[0], r[1]
        glock.acquire()
        for dstid in d: Gnew.add_edge(srcid, dstid, latency=float(d[dstid]['latency']), jitter=float(d[dstid]['jitter']), packetloss=float(0.0))
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
        nwait = doneq.qsize()
        ndone = count + nwait
        est = (((time.time() - start) / ndone) * (npois - ndone)) / 3600.0
        msg = "\rfinished {0}/{1} ({3} waiting), estimated hours remaining: {2}".format(ndone, npois, est, nwait)
        sys.stdout.write(msg)
        sys.stdout.flush()

    # tell workesr to stop
    for w in workers:
        taskq.put('STOP')
        w[2].put('STOP')
#        w[0].join()
#        w[1].join()

    return ensure_nonzero_latency(Gnew)

def run_single(G, pois, Gnew):
    d = nx.all_pairs_dijkstra_path_length(G)
    for srcid in pois:
        for dstid in pois:
            Gnew.add_edge(srcid, dstid, latency=float(d[srcid][dstid]), jitter=float(0.0), packetloss=float(0.0))
    return ensure_nonzero_latency(Gnew)

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

def main():
    num_processes = cpu_count()
    if len(sys.argv) > 1:
        num_processes = int(sys.argv[1])

    # get the original graph
    print "reading graph..."

    G = nx.read_graphml(INPUT_GRAPH)

    # extract the node geocodes and the set of nodes with pop-type as client poi candidates
    print "extracting pois..."

    relays, servers, clients, pops = set(), set(), set(), set()
    nodes = G.nodes()
    for nid in nodes:
        n = G.node[nid]
        if 'type' in n:
            if n['type'] == 'pop': pops.add(nid)
            elif n['type'] == 'relay': relays.add(nid)
            elif n['type'] == 'server': servers.add(nid)
            elif n['type'] == 'client': clients.add(nid)

    # sample the pops and make sure we have at least one from each geocode
    print "selecting clients..."

    codes = {}
    for nid in clients:
        c = G.node[nid]['geocode']
        codes[c] = nid

    clients = set(sample(clients, CLIENT_SAMPLE_SIZE))
    for nid in clients:
        n = G.node[nid]
        c = n['geocode']
        if c in codes: del(codes[c])
    for nid in codes.values(): clients.add(nid)

    print "creating destination graph..."

    pois = clients.union(servers.union(relays))
    Gnew = nx.Graph()
    for id in pois:
        Gnew.add_node(id)
        for attr in G.node[id]:
            Gnew.node[id][attr] = G.node[id][attr]

    print "getting dijkstra shortest path lengths..."

    for (s, d) in G.edges():
        G.edge[s][d]['weight'] = float(G.edge[s][d]['latency'])

    Gnew = run_distributed(num_processes, G, pois, Gnew)
    #Gnew = run_single(G, pois, Gnew)

    print ""
    print "checking graph..."

    assert nx.is_connected(Gnew)
    assert nx.number_connected_components(Gnew) == 1

    print "writing graph..."

    nx.write_graphml(Gnew, OUTPUT_GRAPH)

    print "done!"

if __name__ == '__main__': main()

