#! /usr/bin/python

"""
converts the old [plab] topology to the new graphml format
you MUST wrap the old topology cluster and link tags in a root tag for this to work, e.g.
<top>  ADD THIS
<cluster ...
...
<link ...
...
</top> ADD THIS
"""
import sys, networkx as nx
from lxml import etree

INPUT_FILENAME="topology.xml"
OUTPUT_FILENAME="topology.plab.graphml.xml"

def main():
    parser = etree.XMLParser(huge_tree=True, remove_blank_text=True)
    tree = etree.parse(INPUT_FILENAME, parser)
    root = tree.getroot()

#    itp = etree.iterparse(INPUT_FILENAME, huge_tree=True, remove_blank_text=True)
#    for _, element in itp:
#        print element.tag
#    exit()

    G = nx.Graph()
    poicounter = 0
    d = {}

    for n in root.iterchildren("cluster"):
        c = getcode(n.get("id"))
        poicounter += 1
        d[c] = "poi-{0}".format(poicounter)
        G.add_node(d[c], type="cluster", ip="0.0.0.0", geocode=c, asn=int(0), bandwidthup=int(n.get("bandwidthup")), bandwidthdown=int(n.get("bandwidthdown")), packetloss=float(n.get("packetloss")))

    for l in root.iterchildren("link"):
        codes = l.get("clusters").split()
        csrc, cdst = getcode(codes[0]), getcode(codes[1])
        if csrc not in G.edge or cdst not in G.edge[csrc]:
            G.add_edge(d[csrc], d[cdst], latency=float(l.get("latency")), jitter=float(l.get("jitter")), packetloss=float(0.0))

    # undirected graphs
    assert nx.is_connected(G)
    assert nx.number_connected_components(G) == 1

    # directed graphs
    #assert nx.is_strongly_connected(G)
    #assert nx.number_strongly_connected_components(G) == 1

    nx.write_graphml(G, OUTPUT_FILENAME)

def getcode(code):
    if 'USUS' in code: return "US"
    elif 'CACA' in code: return "CA"
    c = code[0:2]
    if c == "US" or c == "CA": return code
    else: return c

if __name__ == '__main__': sys.exit(main())
