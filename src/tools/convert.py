#!/usr/bin/python3

import argparse
from collections import defaultdict
from typing import Dict, List, Union, Any

import xml.etree.ElementTree as ET
from xml.dom import minidom
from xml.sax.saxutils import unescape

import yaml


XML_TO_YAML = {
    'topology': 'topology',
    'plugin': 'plugins',
    'host': 'hosts',
    'node': 'hosts',
    'process': 'processes',
    'application': 'processes'
}
YAML_TO_XML = {
    'topology': 'topology',
    'plugins': 'plugin',
    'hosts': 'host',
    'processes': 'process'
}


def convert_xml_tag_to_yaml_key(tag: str) -> str:
    '''
    Convert an XML tag in its YAML key
    '''
    return XML_TO_YAML.get(tag, tag)


def convert_yaml_key_to_xml_tag(tag: str) -> str:
    '''
    Convert an YAML key to a XML tag
    '''
    return YAML_TO_XML.get(tag, tag)


def yaml_str_presenter(dumper, data):
    '''
    Convert a string YAML representation to be more human friendly
    '''
    if len(data.splitlines()) > 1:  # check for multiline string
        return dumper.represent_scalar('tag:yaml.org,2002:str', data, style='|')
    return dumper.represent_scalar('tag:yaml.org,2002:str', data)


def yaml_dict_presenter(dumper, data):
    '''
    Allow YAML to conserve the order
    '''
    value = []
    for item_key, item_value in data.items():
        node_key = dumper.represent_data(item_key)
        node_value = dumper.represent_data(item_value)
        value.append((node_key, node_value))
    return yaml.nodes.MappingNode(u'tag:yaml.org,2002:map', value)


def convert_integer(d: Dict[str, Union[str, Any]]) -> Dict[str, Union[str, int, Any]]:
    '''
    Convert the integer elements represented as string in a dict to its int values
    '''
    r = {}

    for k, v in d.items():
        if v.isnumeric():
            r[k] = int(v)
        else:
            r[k] = v
    return r


def xml_to_dict(node: ET.Element) -> Dict:
    '''
    Convert an XML element in dict
    '''
    # Special case, contains network graph as text
    if node.tag == 'topology':
        return {
            'graphml': node.text
        }

    # Iterates over each XML node and transforms those in dict
    if len(node) > 0:
        return {
            **convert_integer(node.attrib),
            **xml_nodes_to_dict(node)
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
        tag = convert_xml_tag_to_yaml_key(xml_node.tag)
        dict_nodes[tag].append(dict_node)

    return dict_nodes


def save_dict_in_yaml_file(d: Dict, stream) -> None:
    '''
    Write a dict as YAML in a stream
    '''
    yaml.add_representer(str, yaml_str_presenter)
    yaml.add_representer(dict, yaml_dict_presenter)
    yaml.dump(d, stream, default_flow_style=False)


def get_output_stream(args: argparse.PARSER, original_extension: str, target_extension: str):
    '''
    Return an opened stream in writen mode with the filename provided in argument
    '''
    if args.output:
        filename = '/dev/stdout' if '-' == args.output else args.output
        return open(filename, 'w', encoding='utf8')
    return open(args.filename.replace(original_extension, target_extension), 'w', encoding='utf8')


def shadow_xml_to_dict(root: ET.Element) -> Dict:
    '''
    Take the Shadow root XML Element and convert it in dict
    '''
    options = convert_integer(root.attrib)

    if options:
        return {
            'options': options,
            **xml_nodes_to_dict(root)
        }
    return {
        **xml_nodes_to_dict(root)
    }


def get_xml_root_from_filename(filename: str) -> ET.Element:
    '''
    Load an XML Element from a XML file
    '''
    tree = ET.parse(filename)
    return tree.getroot()


def get_yaml_from_filename(filename: str) -> Dict:
    '''
    Load an YAML file as dict
    '''
    with open(filename) as yaml_fd:
        return yaml.load(yaml_fd, Loader=yaml.Loader)


def create_xml_root(d: Dict) -> ET.Element:
    '''
    Create the root of the XML element
    '''
    xml_root = ET.Element('shadow')
    if 'options' in d:
        for k, v in d['options'].items():
            xml_root.set(k, str(v))
        del d['options']
    return xml_root


def dict_to_xml(xml_root: ET.Element, d: Dict, tag=None) -> None:
    '''
    Add to an XML element object the elements of a dict
    '''
    attr = {}

    for k, v in d.items():
        if isinstance(v, list):
            list_to_xml(xml_root, k, v)
        elif isinstance(v, dict):
            dict_to_xml(xml_root, v, k)
        else:
            attr[k] = str(v)

    _tag = convert_yaml_key_to_xml_tag(tag)
    ET.SubElement(xml_root, _tag, attrib=attr)


def list_to_xml(xml_root: ET.Element, tag: str, l: List[Dict[str, Union[List, str]]]) -> None:
    '''
    Add to an XML object a list of sub elements
    '''
    for element in l:
        _tag = convert_yaml_key_to_xml_tag(tag)
        sub = ET.SubElement(xml_root, _tag)
        for k, v in sorted(element.items()):
            if isinstance(v, list):
                list_to_xml(sub, k, v)
            else:
                # Special case
                # The graphml attribute is not a tag in XML but a text element
                if 'graphml' == k:
                    sub.text = '<![CDATA[{0}]]>'.format(v)
                else:
                    sub.set(k, str(v))


def save_xml(xml_root: ET.Element, stream) -> None:
    '''
    Write an XML element in a stream
    '''
    _xmlstr = minidom.parseString(ET.tostring(xml_root)).toprettyxml(indent="   ")
    stream.write(unescape(_xmlstr, entities={'&quot;': '"'}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert shadow config files between XML and YAML')
    parser.add_argument('operation', choices=['yaml2xml', 'xml2yaml'])
    parser.add_argument('filename', help='Filename to convert')
    parser.add_argument('--output', help='Output filename', default=None, nargs='?')
    args = parser.parse_args()

    if args.operation == 'yaml2xml':
        yaml_as_dict = get_yaml_from_filename(args.filename)
        xml_root = create_xml_root(yaml_as_dict)
        dict_to_xml(xml_root, yaml_as_dict)
        with get_output_stream(args, 'yaml', 'xml') as stream:
            save_xml(xml_root, stream)
    else:
        xml_root = get_xml_root_from_filename(args.filename)
        d = shadow_xml_to_dict(xml_root)
        with get_output_stream(args, 'xml', 'yaml') as stream:
            save_dict_in_yaml_file(d, stream)
