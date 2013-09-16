#! /usr/bin/python

import networkx as nx


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


"""
G = nx.read_graphml("4-16-2013.xml")
for id in G.nodes_iter():
    n = G.node[id]
    # we already have the id
    del n['nodeid']
    # we can infer the type from the id (ip address or not)
    del n['nodetype']
nx.write_graphml(G, "test.xml")
"""
