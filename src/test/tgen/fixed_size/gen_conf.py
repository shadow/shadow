#!/usr/bin/env python3

import sys
import networkx as nx

def main():
    generate_server()
    generate_client(1, "1 mib", 10, "client.1stream_1mib_10x.graphml")
    generate_client(10, "1 mib", 10, "client.10streams_1mib_10x.graphml")
    generate_client(1, "1 kib", 100, "client.1stream_1kib_100x.graphml")
    generate_client(100, "1 kib", 100, "client.100streams_1kib_100x.graphml")
    generate_client(1, "1 b", 1000, "client.1stream_1b_1000x.graphml")
    generate_client(1000, "1 b", 1000, "client.1000streams_1b_1000x.graphml")

def generate_server():
    G = nx.DiGraph()

    # A tgen timeout is absolute (it stops and marks the stream as failed if it
    # has not completed after N seconds).
    # A tgen stallout is realative (it stops and marks the stream as failed if N
    # seconds have passed since last receiving data on the stream).
    #
    # Here, we increase the stallout time to 60 seconds since we'll be using
    # lots of streams to test slower networks and we want to allow extra time to
    # complete them successfully.
    G.add_node("start", serverport="80", stallout="60 seconds")

    nx.write_graphml(G, "server.graphml")

def generate_client(num_parallel_streams, transfer_size, num_total_transfers, filename):
    G = nx.DiGraph()

    # We start client and server at the same time (in the shadow conf yaml file),
    # but here we tell the client to wait one second to connect to the server.
    #
    # See comment above in `generate_server()` about the stallout time, which
    # we set on both client and server so neither side stalls out early.
    #
    # We set both the `sendsize`` and `recvsize`` here to the `transfer_size`,
    # which means all streams will be bidirectional transfers by default.
    G.add_node("start",
        time="1 second",
        peers="server:80",
        stallout="60 seconds",
        sendsize=transfer_size,
        recvsize=transfer_size)

    # The pause node without a specified pause time attribute acts as a
    # "barrier" that causes tgen to wait until all incoming edges visits that
    # node before it continues its walk through the graph.
    G.add_node("pause")

    # Whenever an end node is visited, tgen will check the total number of
    # completed streams (both successful and failed streams) and stop if it
    # exceeds num_total_transfers.
    G.add_node("end", count=str(num_total_transfers))

    for i in range(num_parallel_streams):
        stream_name = f"stream{i}"

        # We do not set the peeers list or sendsize/recvsize here, so it will
        # grab the defaults from the start node instead.
        G.add_node(stream_name)

        # Creating an unweighted edge from the start node to the stream means
        # that the streams will be launched in parallel.
        G.add_edge("start", stream_name)

        # After every stream completes, it will visit the pause node which will
        # cause tgen to wait until all streams finish before continuing the walk
        # through the graph.
        G.add_edge(stream_name, "pause")

    # After all incoming edges visit the pause node (i.e., all streams complete),
    # tgen moves to the end node and checks the end coniditions.
    G.add_edge("pause", "end")

    # If `num_parallel_streams` < `num_total_transfers`, then we start over with
    # another set of parallel streams.
    G.add_edge("end", "start")

    nx.write_graphml(G, filename)

if __name__ == '__main__':
    sys.exit(main())
