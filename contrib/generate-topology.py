#! /usr/bin/python

import sys, csv, numpy
import networkx as nx

def main():
    # KiB/s
    bwup, bwdown = get_bandwidth("aggregate_mean_netspeeds.csv")
    # fraction between 0 and 1
    loss = get_packet_loss("aggregate_mean_netquality.csv")
    meanloss = numpy.mean(loss.values())
    keys = sorted(bwup.keys())
    for k in keys:
        if k not in loss: loss[k] = meanloss

    bupstr = ','.join(["{0}={1}".format(k, bwup[k]) for k in keys])
    bdownstr = ','.join(["{0}={1}".format(k, bwdown[k]) for k in keys])
    plossstr = ','.join(["{0}={1}".format(k, loss[k]) for k in keys])

    # now get the graph and mod it as needed for shadow
    Gin = nx.read_graphml("inet-test.graphml.xml")

    G = nx.DiGraph()
    G.graph['bandwidthup'] = bupstr
    G.graph['bandwidthdown'] = bdownstr
    G.graph['packetloss'] = plossstr
    # hack until we use a version of igraph that supports graph attributes
    G.add_node("dummynode", bandwidthup=bupstr, bandwidthdown=bdownstr, packetloss=plossstr)

    for id in Gin.nodes():
        n = Gin.node[id]
        if 'nodetype' not in n: continue

        nodetypestr = 'server' if 'dest' in n['nodetype'] else n['nodetype']
        geocodesstr = n['countries'] if 'countries' in n else 'US'
        asnstr = n['asn'].split()[0][2:] if 'asn' in n else '0'

        G.add_node(get_id_string(id), nodetype=nodetypestr, geocodes=geocodesstr, asn=asnstr) 

    for (srcid,dstid) in Gin.edges():
        e = Gin.edge[srcid][dstid]
        l = e['latency']
        G.add_edge(get_id_string(srcid), get_id_string(dstid), latencies=l)

    # make sure the dummy node is connected
    dummy_connect_id = G.nodes()[0]
    G.add_edge("dummynode", dummy_connect_id, latencies="10000.0")
    G.add_edge(dummy_connect_id, "dummynode", latencies="10000.0")

    nx.write_graphml(G, "topology.graphml.xml")

def get_id_string(id):
    return '.'.join(id.split('_')[1:]) if 'dest' in id else str(id)

def get_packet_loss(filename):
    loss = {}
    with open('aggregate_mean_netquality.csv', 'rb') as f:
        r = csv.reader(f) # country, region, jitter, packetloss, latency
        for row in r:
            country, region, jitter, packetloss, latency = row[0], row[1], float(row[2]), float(row[3]), float(row[4])
            packetloss /= 100.0 # percent to fraction
            assert packetloss > 0.0 and packetloss < 1.0
            code = get_code(country, region)
            if code not in loss: loss[code] = packetloss
    return loss

def get_bandwidth(filename):
    bwdown, bwup = {}, {}
    with open('aggregate_mean_netspeeds.csv', 'rb') as f:
        r = csv.reader(f) # country, region, bwdown, bwup
        for row in r:
            country, region = row[0], row[1]
            # kb/s -> KiB/s
            down = int(round((float(row[2]) * 1000.0) / 1024.0))
            up = int(round((float(row[3]) * 1000.0) / 1024.0))
            code = get_code(country, region)
            if code not in bwdown: bwdown[code] = down
            if code not in bwup: bwup[code] = up
    return bwdown, bwup

def get_code(country, region):
    if ('US' in country and 'US' not in region) or ('CA' in country and 'CA' not in region):
        return "{0}{1}".format(country, region)
    else:
        return "{0}".format(country)

def make_test_graph():
    G = nx.DiGraph()

    G.graph['bandwidthup'] = "USDC=1024,USVA=1024,USMD=968,FR=600,DE=750"
    G.graph['bandwidthdown'] = "USDC=1024,USVA=1024,USMD=968,FR=600,DE=750"
    G.graph['packetloss'] = "USDC=0.001,USVA=0.001,USMD=0.001,FR=0.001,DE=0.001"

    G.add_node("141.161.20.54", nodetype="relay", nodeid="141.161.20.54", asn=10, geocodes="USDC")
    G.add_node("1", nodetype="pop", nodeid="1", asn=10, geocodes="USDC,USVA,USMD")
    G.add_node("2", nodetype="pop", nodeid="2", asn=20, geocodes="FR,DE")
    G.add_node("137.150.145.240", nodetype="server", nodeid="137.150.145.240", asn=30, geocodes="DE")

    G.add_edge("141.161.20.54", "1", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")
    G.add_edge("1", "141.161.20.54", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")
    G.add_edge("1", "2", latencies="80.3,83.6,88.5,89.4,89.6,89.9,90.9,91.2,92.3,95.0")
    G.add_edge("2", "1", latencies="80.3,83.6,88.5,89.4,89.6,89.9,90.9,91.2,92.3,95.0")
    G.add_edge("2", "137.150.145.240", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")
    G.add_edge("137.150.145.240", "2", latencies="2.0,2.1,2.2,2.2,2.2,2.3,2.6,2.8,3.0,3.5")

    nx.write_graphml(G, "test.graphml.xml")

if __name__ == '__main__': sys.exit(main())
