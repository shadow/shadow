#! /usr/bin/python

import sys, csv, numpy
import networkx as nx

BW_FILENAME="aggregate_mean_netspeeds.csv"
PLOSS_FILENAME="aggregate_mean_netquality.csv"
MAP_FILENAME="full_tor_map.xml"
GEO_FILENAME="geoip"
OUTPUT_FILENAME="topology.full.graphml.xml"

def main():
    bwdown, bwup = get_bandwidth() # KiB/s
    loss = get_packet_loss() # fraction between 0 and 1
    medloss = numpy.median(loss.values()) # resist outliers
    keys = sorted(bwup.keys())
    for k in keys:
        if k not in loss: loss[k] = medloss

    geo = get_geo()

    # now get the graph and mod it as needed for shadow
    Gin = nx.read_graphml(MAP_FILENAME)
    assert nx.is_connected(Gin)
    assert nx.number_connected_components(Gin) == 1
    print "G in appears OK"

    G = nx.Graph()

    fp_to_ip = {}
    id_to_id = {}

    popcounter, poicounter = 0, 0
    for id in Gin.nodes():
        n = Gin.node[id]

        if 'nodetype' not in n: continue

        elif 'pop' in n['nodetype']:
            gc = 'A1'
            if 'countries' in n: gc = n['countries']
            elif 'country' in n: gc = n['country']
            else: print "unknown country code for pop {0}, using {1}".format(id, gc)
            asnum = n['asn'].split()[0][2:]
            popcounter += 1
            newid = "pop-{0}".format(popcounter)
            id_to_id[id] = newid
            G.add_node(newid, type='pop', geocode=gc, asn=int(asnum))

            if gc in bwup:
                poicounter += 1
                clientid = "poi-{0}".format(poicounter)
                G.add_node(clientid, type='client', ip="0.0.0.0", geocode=gc, asn=int(asnum), bandwidthup=int(bwup[gc]), bandwidthdown=int(bwdown[gc]), packetloss=float(loss[gc]))
                G.add_edge(clientid, newid, latency=float(5.0), jitter=float(0.0), packetloss=float(0.0))

        elif 'relay' in n['nodetype']:
            ip = n['relay_ip']
            fingerprint = n['id']
            #nickname = n['nick']
            asnum = n['asn'].split()[0][2:]
            #pop = n['pop']
            #poiip = n['ip']

            gc = 'A1'
            if 'country' in n and n['country'] != 'EU': gc = n['country']
            elif 'countries' in n and n['countries'] != 'EU': gc = n['countries']
            else: gc = get_geo_code(geo, ip)

            if gc in bwup:
                poicounter += 1
                newid = "poi-{0}".format(poicounter)
                id_to_id[id] = newid
                G.add_node(newid, type='relay', ip=ip, geocode=gc, asn=int(asnum), bandwidthup=int(bwup[gc]), bandwidthdown=int(bwdown[gc]), packetloss=float(loss[gc]))

        elif 'dest' in n['nodetype']:
            ip = '.'.join(n['nodeid'].split('_')[1:])
            asnum = n['asn'].split()[0][2:]
            gc = get_geo_code(geo, ip)
            #gc = n['country']
            if gc in bwup:
                poicounter += 1
                newid = "poi-{0}".format(poicounter)
                id_to_id[id] = newid
                G.add_node(newid, type='server', ip=ip, geocode=gc, asn=int(asnum), bandwidthup=int(bwup[gc]), bandwidthdown=int(bwdown[gc]), packetloss=float(loss[gc]))

    for (srcid, dstid) in Gin.edges():
        if srcid in id_to_id and dstid in id_to_id:
            e = Gin.edge[srcid][dstid]
            l = [float(i) for i in e['latency'].split(',')]
            meanl = numpy.mean(l)
            jit = (l[7] - l[2]) / 2.0
            G.add_edge(id_to_id[srcid], id_to_id[dstid], latency=float(meanl), jitter=float(jit), packetloss=float(0.0))
        else: print "skipped edge: {0} -- {1}".format(srcid, dstid)

    # undirected graphs
    assert nx.is_connected(G)
    assert nx.number_connected_components(G) == 1

    # directed graphs
    #assert nx.is_strongly_connected(G)
    #assert nx.number_strongly_connected_components(G) == 1

    print "G out is connected with 1 component"

    nx.write_graphml(G, OUTPUT_FILENAME)

def convert_packet_loss(loss):
    pl = loss / 100.0 # percent to fraction
    rel = 1.0 - pl # reliability
    rel = numpy.sqrt(rel) # reliability up to core, sqrt b/c this will be mult by itself (srcrel*dstrel) when src and dst are in the same vertex
    pl = 1.0 - rel
    assert pl > 0.0 and pl < 1.0
    return pl

def get_packet_loss():
    loss = {}
    with open(PLOSS_FILENAME, 'rb') as f:
        r = csv.reader(f) # country, region, jitter, packetloss, latency
        for row in r:
            country, region, jitter, packetloss, latency = row[0], row[1], float(row[2]), convert_packet_loss(float(row[3])), float(row[4])
            code = get_code(country, region)
            if code not in loss: loss[code] = packetloss
    return loss

def get_bandwidth():
    bwdown, bwup = {}, {}
    with open(BW_FILENAME, 'rb') as f:
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

def get_geo_code(geo, ip):
    ip_array = ip.split('.')
    ipnum = long(ip_array[0]) * 16777216 + long(ip_array[1]) * 65536 + long(ip_array[2]) * 256 + long(ip_array[3])
    for entry in geo:
        if ipnum >= entry[0] and ipnum <= entry[1]: 
            return "{0}".format(entry[2])
    return "US"

def get_geo():
    entries = []
    with open(GEO_FILENAME, "rb") as f:
        for line in f:
            if line[0] == "#": continue
            parts = line.strip().split(',')
            entry = (parts[0], parts[1], parts[2]) # lownum, highnum, countrycode
            entries.append(entry)
    return entries

# this is no longer up to date
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
