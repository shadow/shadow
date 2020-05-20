#!/usr/bin/python3

import sys
from collections import defaultdict
import xml.etree.ElementTree as ET

import yaml


def xml_to_dict(node):
    # Special case, contains network graph as text
    if node.tag == 'topology':
        return {
            'graphml': node.text
        }

    # Iterates over each XML node and transforms those in dict
    if node.getchildren():
        return {
            **node.attrib,
            **xml_nodes_to_dict(node.getchildren())
        }

    # No sub XML nodes included in this node, returns node attributes only
    return node.attrib


def xml_nodes_to_dict(xml_nodes):
    '''
    Iterates over XML nodes and transforms them in dict
    '''
    dict_nodes = defaultdict(list)

    for xml_node in xml_nodes:
        dict_node = xml_to_dict(xml_node)
        tag = xml_node.tag
        dict_nodes[tag].append(dict_node)

    return dict_nodes


def save_dict_in_yaml_file(d, filename):
    with open(filename, 'w', encoding='utf8') as f:
        _yaml = yaml.dump(d, f)


def shadow_xml_to_dict(root):
    return {
        'option': root.attrib,
        **xml_nodes_to_dict(root.getchildren())
    }


def get_xml_root_from_filename(filename):
    tree = ET.parse('./file.xml')
    return tree.getroot()


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} <xml_filename> <yaml_filename>', file=sys.stderr)
        exit(1)

    xml_filename = sys.argv[1]
    yaml_filename = sys.argv[2]

    xml_root = get_xml_root_from_filename(xml_filename)
    d = shadow_xml_to_dict(xml_root)
    save_dict_in_yaml_file(d, yaml_filename)
