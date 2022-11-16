#!/usr/bin/env python3

import sys
import networkx as nx

def main():
    generate_server()
    generate_client(1, "client.1stream.graphml")
    generate_client(10, "client.10streams.graphml")
    generate_client(100, "client.100streams.graphml")
    generate_client(1000, "client.1000streams.graphml")

def generate_server():
    G = nx.DiGraph()

    # A tgen timeout is absolute (it stops and marks the stream as failed if it
    # has not completed after N seconds).
    #
    # Here, we reduce the timeout time to 5 seconds since we want tgen to stop
    # after that much time even though the transfers will not be complete yet.
    G.add_node("start", serverport="80", timeout="5 seconds")

    nx.write_graphml(G, "server.graphml")

def generate_client(num_parallel_streams, filename):
    G = nx.DiGraph()

    # We start client and server at the same time (in the shadow conf yaml file),
    # but here we tell the client to wait one second to connect to the server.
    #
    # See comment above in `generate_server()` about the timeout time, which we
    # set on both client and server so both sides timeout early (though this is
    # not strictly necessary, since a timeout on either side would end the
    # transfer).
    #
    # We set both the sendsize and recvsize here to a very large size to
    # effectively emulate an unlimited transfer. Setting both `sendsize` and
    # `recvsize` means that all streams will be bidirectional transfers by
    # default.
    G.add_node("start",
        time="1 second",
        peers="server:80",
        timeout="5 seconds",
        sendsize="10 gib",
        recvsize="10 gib")

    # Whenever any stream completes, tgen will move to the end node and stop
    # because we set the `count` here to 1. We don't expect that this will ever
    # occur in practice wsince we are using very large file sizes.
    G.add_node("end", count="1")

    for i in range(num_parallel_streams):
        stream_name = f"stream{i}"
        # We do not set the peeers list or sendsize/recvsize here, so it will
        # grab the defaults from the start node instead.
        G.add_node(stream_name)

        # Creating an unweighted edge from the start node to the stream means
        # that the streams will be launched in parallel.
        G.add_edge("start", stream_name)

        # After any stream completes, tgen will visit the end node to check the
        # completed stream count.
        G.add_edge(stream_name, "end")

    # The graph must be connected, so we add this edge, but it will never be
    # visited with our current setup.
    G.add_edge("end", "start")

    nx.write_graphml(G, filename)

if __name__ == '__main__':
    sys.exit(main())
