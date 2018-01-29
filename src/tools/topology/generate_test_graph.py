import networkx as nx
from cStringIO import StringIO

G = nx.Graph(preferdirectpaths="True")
G.add_node("poi-1", ip="11.1.2.3", citycode="123456", countrycode="UK", type="client", bandwidthdown=17038, bandwidthup=2251, packetloss=0.0)
G.add_node("poi-2", ip="0.0.0.0", citycode="37465", countrycode="US", type="server", bandwidthdown=10000, bandwidthup=5000)
G.add_node("poi-3", ip="124.1.1.1", citycode="987654", geocode="DE", type="relay", bandwidthdown=17038, bandwidthup=2251, packetloss=0.0)

# we want to test that when preferdirectpaths is set, it does actually take the longer direct
# path rather than the shorter indirect path that it would normally prefer
G.add_edge("poi-1", "poi-2", latency=10.0, packetloss=0.05)
G.add_edge("poi-2", "poi-3", latency=10.0, jitter=0.0, packetloss=0.05)
G.add_edge("poi-1", "poi-3", latency=50.0, jitter=0.0, packetloss=0.05)

s = StringIO()
nx.write_graphml(G, s)
print s.getvalue(),
