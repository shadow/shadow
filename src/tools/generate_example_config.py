#!/usr/bin/env python3

import sys, networkx as nx
from lxml import etree
try:
    from cStringIO import StringIO
except ImportError:
    from io import BytesIO as StringIO


def main():
    generate_shadow()
    generate_tgen_server()
    generate_tgen_client()

def generate_shadow():
    root = etree.Element("shadow")
    root.set("stoptime", "3600")

    e = etree.SubElement(root, "topology")
    e.text = etree.CDATA(get_topology())

    e = etree.SubElement(root, "plugin")
    e.set("id", "tgen")
    e.set("path", "~/.shadow/bin/tgen")

    e = etree.SubElement(root, "node")
    e.set("id", "server")
    e.set("quantity", "2")
    a = etree.SubElement(e, "application")
    a.set("plugin", "tgen")
    a.set("starttime", "1")
    a.set("arguments", "tgen.server.graphml.xml")

    e = etree.SubElement(root, "node")
    e.set("id", "client")
    a = etree.SubElement(e, "application")
    a.set("plugin", "tgen")
    a.set("starttime", "2")
    a.set("arguments", "tgen.client.graphml.xml")

    with open("shadow.config.xml", 'wb') as f:
        f.write(etree.tostring(root, pretty_print=True, xml_declaration=False))

def generate_tgen_server():
    G = nx.DiGraph()
    G.add_node("start", serverport="8888")
    nx.write_graphml(G, "tgen.server.graphml.xml")

def generate_tgen_client():
    G = nx.DiGraph()

    G.add_node("start", serverport="8888", time="60", peers="server1:8888,server2:8888")

    G.add_node("transfer", type="get", protocol="tcp", size="1 MiB")
    G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10")
    G.add_node("end", time="3600", count="100", size="100 MiB")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "end")
    G.add_edge("end", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, "tgen.client.graphml.xml")

def get_topology():
    G = nx.Graph()
    G.add_node("poi-1", packetloss=0.0, ip="0.0.0.0", countrycode="US", bandwidthdown=17038, bandwidthup=2251)
    G.add_edge("poi-1", "poi-1", latency=50.0, packetloss=0.05)
    s = StringIO()
    nx.write_graphml(G, s)
    return s.getvalue()

if __name__ == '__main__': sys.exit(main())
