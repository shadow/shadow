#!/usr/bin/python3

import argparse
from collections import defaultdict
import xml.etree.ElementTree as ET
from xml.dom import minidom
from xml.sax.saxutils import unescape

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
        _yaml = yaml.dump(d, f, default_flow_style=False, sort_keys=False)


def get_filename_converted(original_filename, optional_filename_result, original_extension, target_extension):
    if optional_filename_result:
        return optional_filename_result
    return original_filename.replace(original_extension, target_extension)


def shadow_xml_to_dict(root):
    return {
        'options': convert_integer(root.attrib),
        **xml_nodes_to_dict(root.getchildren())
    }


def get_xml_root_from_filename(filename):
    tree = ET.parse(filename)
    return tree.getroot()


def get_yaml_from_filename(filename):
    with open(filename) as yaml_fd:
        return yaml.load(yaml_fd, Loader=yaml.Loader)


def get_xml_root(yaml_as_dict):
    xml_root = ET.Element('shadow')
    if 'options' in yaml_as_dict:
        for k, v in yaml_as_dict['options'].items():
            xml_root.set(k, str(v))
        del yaml_as_dict['options']
    return xml_root


def dict_to_xml(xml_root, yaml_as_dict, tag=None):
    attr = {}
    for k, v in yaml_as_dict.items():
        if isinstance(v, list):
            list_to_xml(xml_root, k, v)
        elif isinstance(v, dict):
            dict_to_xml(xml_root, v, k)
        else:
            attr[k] = str(v)
    ET.SubElement(xml_root, tag, attrib=attr)


def list_to_xml(xml_root, tag, l):
    for element in l:
        sub = ET.SubElement(xml_root, tag)
        for k, v in element.items():
            if isinstance(v, list):
                list_to_xml(sub, k, v)
            else:
                if 'graphml' == k:
                    sub.text = f'<![CDATA[{v}]]>'
                else:
                    sub.set(k, str(v))


def save_xml(xml_root, filename_converted):
    xmlstr = minidom.parseString(ET.tostring(xml_root)).toprettyxml(indent="   ")
    with open(filename_converted, "w", encoding='utf8') as f:
        f.write(unescape(xmlstr, entities={'&quot;': '"'}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert a XML file into a YAML file.')
    parser.add_argument('operation', help='yaml2xml or xml2yaml')
    parser.add_argument('filename', help='Filename to convert')
    parser.add_argument('--output', help='Output filename', default=None, nargs='?')
    args = parser.parse_args()

    if args.operation == 'yaml2xml':
        yaml_as_dict = get_yaml_from_filename(args.filename)
        xml_root = get_xml_root(yaml_as_dict)
        dict_to_xml(xml_root, yaml_as_dict)
        filename_converted = get_filename_converted(args.filename, args.output, 'yaml', 'xml')
        save_xml(xml_root, filename_converted)
    else:
        xml_root = get_xml_root_from_filename(args.filename)
        d = shadow_xml_to_dict(xml_root)

        filename_converted = get_filename_converted(args.filename, args.output, 'xml', 'yaml')
        save_dict_in_yaml_file(d, filename_converted)
