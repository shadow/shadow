#!/usr/bin/python

import sys, networkx as nx
from lxml import etree
from cStringIO import StringIO

def main():
    generate_shadow()
    generate_tgen_server()
    generate_tgen_client()

def generate_shadow():
    root = etree.Element("shadow")

    e = etree.SubElement(root, "kill")
    e.set("time", "3600")

    e = etree.SubElement(root, "topology")
    e.text = etree.CDATA(get_topology())

    e = etree.SubElement(root, "plugin")
    e.set("id", "tgen")
    e.set("path", "~/.shadow/plugins/libshadow-plugin-tgen.so")

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

    with open("shadow.config.xml", 'wb') as f: print >>f, etree.tostring(root, pretty_print=True, xml_declaration=False)

def generate_tgen_server():
    G = nx.DiGraph()
    G.add_node("start", serverport="8888", time="0")
    nx.write_graphml(G, "tgen.server.graphml.xml")

def generate_tgen_client():
    G = nx.DiGraph()

    G.add_node("start", serverport="8888", time="60", peers="server1:8888,server2:8888")

    G.add_node("transferA", type="get", protocol="tcp", size="1 MiB", peers="server1:8888")
    G.add_node("transferB1", type="get", protocol="tcp", size="100 KiB")
    G.add_node("transferB2", type="get", protocol="tcp", size="100 KiB")
    G.add_node("transferB3", type="get", protocol="tcp", size="100 KiB")
    G.add_node("transferB4", type="get", protocol="tcp", size="100 KiB")
    G.add_node("transferB5", type="get", protocol="tcp", size="100 KiB")
    G.add_node("transferC", type="put", protocol="tcp", size="256 KiB")

    G.add_node("synchronize")
    G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10")
    G.add_node("end", time="3600", count="100", size="100 MiB")

    G.add_edge("start", "transferA")

    G.add_edge("transferA", "transferB1")
    G.add_edge("transferA", "transferB2")
    G.add_edge("transferA", "transferB3")
    G.add_edge("transferA", "transferB4")
    G.add_edge("transferA", "transferB5")

    G.add_edge("transferB1", "synchronize")
    G.add_edge("transferB2", "synchronize")
    G.add_edge("transferB3", "synchronize")
    G.add_edge("transferB4", "synchronize")
    G.add_edge("transferB5", "synchronize")

    G.add_edge("synchronize", "transferC")

    G.add_edge("transferC", "end")
    G.add_edge("end", "pause")
    G.add_edge("pause", "start")

    nx.write_graphml(G, "tgen.client.graphml.xml")

def get_topology():
    G = nx.Graph()
    G.add_node("poi-1", packetloss=0.0, ip="0.0.0.0", geocode="US", bandwidthdown=17038, bandwidthup=2251, type="net", asn=0)
    G.add_edge("poi-1", "poi-1", latency=50.0, jitter=0.0, packetloss=0.05)
    s = StringIO()
    nx.write_graphml(G, s)
    return s.getvalue()

if __name__ == '__main__': sys.exit(main())
