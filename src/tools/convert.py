#!/usr/bin/python3

import sys
import argparse
from collections import defaultdict
import xml.etree.ElementTree as ET

import yaml


def yaml_str_presenter(dumper, data):
    if len(data.splitlines()) > 1:  # check for multiline string
        return dumper.represent_scalar('tag:yaml.org,2002:str', data, style='|')
    return dumper.represent_scalar('tag:yaml.org,2002:str', data)


def convert_integer(d):
    r = {}

    for k, v in d.items():
        if v.isnumeric():
            r[k] = int(v)
        else:
            r[k] = v
    return r


def xml_to_dict(node):
    # Special case, contains network graph as text
    if node.tag == 'topology':
        return {
            'graphml': node.text
        }

    # Iterates over each XML node and transforms those in dict
    if node.getchildren():
        return {
            **convert_integer(node.attrib),
            **xml_nodes_to_dict(node.getchildren())
        }

    # No sub XML nodes included in this node, returns node attributes only
    return convert_integer(node.attrib)


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
        yaml.add_representer(str, yaml_str_presenter)
        _yaml = yaml.dump(d, f)


def get_filename_converted(original_filename, optional_filename_result, original_extension, target_extension):
    if optional_filename_result:
        return optional_filename_result
    return original_filename.replace(original_extension, target_extension)


def shadow_xml_to_dict(root):
    return {
        'option': convert_integer(root.attrib),
        **xml_nodes_to_dict(root.getchildren())
    }


def get_xml_root_from_filename(filename):
    tree = ET.parse('./file.xml')
    return tree.getroot()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert a XML file into a YAML file.')
    parser.add_argument('operation', help='yaml2xml or xml2yaml')
    parser.add_argument('filename', help='Filename to convert')
    parser.add_argument('--output', help='Output filename', default=None, nargs='?')
    args = parser.parse_args()

    if args.operation == 'yaml2xml':
        print('Not yet implemented', file=sys.stderr)s

    xml_root = get_xml_root_from_filename(args.filename)
    d = shadow_xml_to_dict(xml_root)

    filename_converted = get_filename_converted(args.filename, args.output, 'xml', 'yaml')
    save_dict_in_yaml_file(d, filename_converted)
