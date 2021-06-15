#!/usr/bin/env python3

import argparse
from collections import defaultdict
from typing import Dict, List, Union, Any

import xml.etree.ElementTree as ET
from xml.dom import minidom
from xml.sax.saxutils import unescape

import networkx as nx

import yaml
import sys
import io


def bandwidth_conversion(x):
    # shadow classic uses bandwidths units of KiB/s
    return str(int(x) * 8) + ' Kibit'


def latency_conversion(x):
    # shadow classic uses latency units of milliseconds
    x = float(x)
    assert x.is_integer(), 'Sub-millisecond latencies are not supported'
    return str(int(x)) + ' ms'


# original attribute name: (new attribute name, new attribute type, value transform fn)
ATTR_CONVERSIONS = {
    'bandwidthup': ('bandwidth_up', 'string', bandwidth_conversion),
    'bandwidthdown': ('bandwidth_down', 'string', bandwidth_conversion),
    'latency': ('latency', 'string', latency_conversion),
    'packetloss': ('packet_loss', None, None),
    'countrycode': ('country_code', None, None),
    'citycode': ('city_code', None, None),
    'geocode': ('geo_code', None, None),
    'ip': ('ip_address', None, None),
}


def convert_topology(xml_root, out_file, set_labels):
    graphml_ns = 'http://graphml.graphdrawing.org/xmlns'
    graphml_ns_prefix = '{' + graphml_ns + '}'

    ET.register_namespace('', graphml_ns)

    # map of functions to apply to text/values for elements with a given id
    id_type_conversions = {}

    # remap any of the attribute names and types, and build `id_type_conversions`
    for x in xml_root.findall('{}key'.format(graphml_ns_prefix)):
        if x.attrib['attr.name'] in ATTR_CONVERSIONS:
            (attr_name, attr_type,
             attr_map_fn) = ATTR_CONVERSIONS[x.attrib['attr.name']]
            x.attrib['attr.name'] = attr_name
            if attr_type != None:
                x.attrib['attr.type'] = attr_type
            if attr_map_fn != None:
                id_type_conversions[x.attrib['id']] = attr_map_fn

    # transform the text/values
    for x in xml_root.findall('{}graph'.format(graphml_ns_prefix)):
        for y in x.findall('{}data'.format(graphml_ns_prefix)):
            if y.attrib['key'] in id_type_conversions:
                y.text = id_type_conversions[y.attrib['key']](y.text)
        nodes = x.findall('{}node'.format(graphml_ns_prefix))
        edges = x.findall('{}edge'.format(graphml_ns_prefix))
        for y in nodes + edges:
            for z in y.findall('{}data'.format(graphml_ns_prefix)):
                if z.attrib['key'] in id_type_conversions:
                    z.text = id_type_conversions[z.attrib['key']](z.text)

    removed_graph_keys = {}
    removed_graph_data = {}

    # collect and remove the keys for any unsupported graph attributes
    for x in xml_root.findall('{}key'.format(graphml_ns_prefix)):
        # if x.attrib['attr.name'] in UNSUPPORTED_ATTRS:
        if x.attrib['for'] == 'graph':
            removed_graph_keys[x.attrib['id']] = x.attrib['attr.name']
            xml_root.remove(x)

    # store and remove the text/values for any unsupported graph attributes
    for x in xml_root.findall('{}graph'.format(graphml_ns_prefix)):
        for y in x.findall('{}data'.format(graphml_ns_prefix)):
            if y.attrib['key'] in removed_graph_keys:
                removed_graph_data[removed_graph_keys[y.attrib['key']]] = y.text
                x.remove(y)

    # build the graph from the xml
    xml = ET.tostring(xml_root, encoding='utf-8', method='xml').decode('utf-8')
    graph = nx.parse_graphml(xml)

    # shadow doesn't use any attributes that would go in 'node_default' or
    # 'edge_default', so we don't expect there to be any
    if 'node_default' in graph.graph:
        assert len(graph.graph['node_default']) == 0
        del graph.graph['node_default']

    if 'edge_default' in graph.graph:
        assert len(graph.graph['edge_default']) == 0
        del graph.graph['edge_default']

    # Only change node and edge labels if the label option was given.
    # Our custom labels help avoid cross-ref from an edge to a node via the node's
    # numeric id, which can be a pain especially on large graphs.
    if set_labels:
        graph = nx.relabel_nodes(graph, lambda x: f"node at {graph.nodes[x]['ip_address']}")
        for (source, target) in graph.edges:
            src_ip = graph.nodes[source]['ip_address']
            tgt_ip = graph.nodes[target]['ip_address']
            graph[source][target]['label'] = f"path from {src_ip} to {tgt_ip}"

    # generate gml from the graph
    try:
        nx.write_gml(graph, out_file)
    except nx.exception.NetworkXError:
        # we require keys with underscores which isn't technically allowed by the
        # spec, but both igraph and recent versions of networkx allow them
        print("Unable to write GML. Do you have networkx version >= 2.5?",
              file=sys.stderr)
        raise

    return removed_graph_data


if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description=('Convert Shadow topology files from the version 1.x format '
                     'to the 2.x format, and write the output to stdout'))
    parser.add_argument('filename', help='Filename to convert')
    parser.add_argument('--set-labels', help='Add custom labels to nodes and edges',
                        action="store_true", default=False, required=False)
    args = parser.parse_args()

    filename = args.filename
    if filename == '-':
        filename = '/dev/stdin'

    tree = ET.parse(filename)

    # We remove and show a warning for deprecated graph attributes, but leave deprecated
    # node/edge attributes alone. Shadow currently refuses to read graphs with unrecognized
    # node/edge attributes so the user will be able to tell if any are no longer used, but
    # igraph is unable to read GML graph attributes and will not raise an error, so we show
    # the error here instead. In the case of 'preferdirectpaths', this also lets us convert
    # the option to 'use_shortest_path' in 'convert_legacy_config.py' without needing to
    # raise an error.

    removed_graph_data = convert_topology(tree.getroot(), sys.stdout.buffer, args.set_labels)

    for x in removed_graph_data:
        print('Removed the deprecated graph attribute \'{}\': {}'.format(
            x, removed_graph_data[x]), file=sys.stderr)
