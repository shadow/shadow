#!/usr/bin/python

import networkx as nx

G = nx.DiGraph()

G.add_node("start", time="60", serverport="8888", socksproxy="webserver1:9050", peers="webserver1:8888,webserver2:8888,webserver3:8888,webserver4:8888,webserver5:8888")

G.add_node("transfer1", type="get", protocol="tcp", size="1 MiB", peers="webserver1:8888,webserver2:8888")
G.add_node("transfer2", type="get", protocol="tcp", size="1 MiB", peers="webserver1:8888,webserver2:8888")
G.add_node("transfer3", type="get", protocol="tcp", size="1 MiB", peers="webserver1:8888,webserver2:8888")
G.add_node("transfer4", type="put", protocol="tcp", size="256 KiB", peers="webserver1:8888,webserver2:8888")

G.add_node("synchronize")
G.add_node("end", time="3600", count="10", size="10 MiB")
G.add_node("pause", time="5")

G.add_edge("start", "transfer1")
G.add_edge("transfer1", "transfer2")
G.add_edge("transfer1", "transfer3")
G.add_edge("transfer3", "transfer4")
G.add_edge("transfer2", "synchronize")
G.add_edge("transfer4", "synchronize")
G.add_edge("synchronize", "end")
G.add_edge("end", "pause")
G.add_edge("pause", "start")

nx.write_graphml(G, "tgen.test.graphml.xml")

