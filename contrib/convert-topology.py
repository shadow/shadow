#! /usr/bin/python

"""
converts the old topology to the new graphml format
you MUST wrap the old topology cluster and link tags in a root tag for this to work, e.g.
<top>  ADD THIS
<cluster ...
...
<link ...
...
</top> ADD THIS
"""
import sys
import networkx as nx
from lxml import etree

def main():
    if len(sys.argv) != 3: print >>sys.stderr, "{0} input.xml output.xml".format(sys.argv[0]);exit()

    infname = sys.argv[1]
    outfname = sys.argv[2]

    parser = etree.XMLParser(huge_tree=True, remove_blank_text=True)
    tree = etree.parse(infname, parser)
    root = tree.getroot()

#    itp = etree.iterparse(infname, huge_tree=True, remove_blank_text=True)
#    for _, element in itp:
#        print element.tag
#    exit()

    ndata = {}
    for n in root.iterchildren("cluster"):
        c = getcode(n.get("id"))
        bwdown = int(n.get("bandwidthdown"))
        bwup = int(n.get("bandwidthup"))
        loss = float(n.get("packetloss"))/100.0
        ndata[c] = {}
        ndata[c]['up'] = bwup
        ndata[c]['down'] = bwdown
        ndata[c]['loss'] = loss

    ldata = {}
    for l in root.iterchildren("link"):
        codes = l.get("clusters").split()
        csrc = getcode(codes[0])
        cdst = getcode(codes[1])
        latency = [int(l.get("latency"))]
        if l.get("latencyQ1") != None: latency.insert(0, int(l.get("latencyQ1")))
        if l.get("latencyQ3") != None: latency.append(int(l.get("latencyQ3")))
        lstr = ','.join([str(lat) for lat in latency])
        if csrc not in ldata: ldata[csrc] = {}
        ldata[csrc][cdst] = lstr
        if cdst not in ldata: ldata[cdst] = {}
        ldata[cdst][csrc] = lstr
    
    keys = sorted(ndata.keys())
    bupstr = ','.join(["{0}={1}".format(k, ndata[k]['up']) for k in keys])
    bdownstr = ','.join(["{0}={1}".format(k, ndata[k]['down']) for k in keys])
    plossstr = ','.join(["{0}={1}".format(k, ndata[k]['loss']) for k in keys])

    G = nx.DiGraph()
    G.graph['bandwidthup'] = bupstr
    G.graph['bandwidthdown'] = bdownstr
    G.graph['packetloss'] = plossstr

    G.add_node("dummynode", bandwidthup=bupstr, bandwidthdown=bdownstr, packetloss=plossstr)
    G.add_edge("dummynode", keys[0], latencies="0.0")
    G.add_edge(keys[0], "dummynode", latencies="0.0")

    ips = {}
    a, b, c, d = 1, 0, 0, 1
    for k in keys:
        ip = "{0}.{1}.{2}.{3}".format(a,b,c,d)
        ips[k] = ip
        G.add_node(ip, nodetype="server", geocodes=k, asn=0)
        d += 1
        if d > 250: d = 0; c += 1
        if c > 250: c = 0; b += 1

    for srck in sorted(ldata.keys()):
        G.add_edge(ips[srck], "dummynode", latencies="1000")
        G.add_edge("dummynode", ips[srck], latencies="1000")
        for dstk in sorted(ldata[srck].keys()):
            G.add_edge(ips[srck], ips[dstk], latencies=ldata[srck][dstk])

    nx.write_graphml(G, outfname)

def getcode(code):
    if 'USUS' in code: return "US"
    elif 'CACA' in code: return "CA"
    c = code[0:2]
    if c == "US" or c == "CA": return code
    else: return c

if __name__ == '__main__': sys.exit(main())
