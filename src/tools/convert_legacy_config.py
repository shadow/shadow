#!/usr/bin/env python3

import argparse
from collections import defaultdict
from typing import Dict, List, Union, Any

import xml.etree.ElementTree as ET
from xml.dom import minidom
from xml.sax.saxutils import unescape

import yaml
import sys
import io
import os

from convert_legacy_topology import convert_topology

XML_TAGS_TO_YAML = {
    'plugin': 'plugins',
    'topology': 'network',
    'host': 'hosts',
    'node': 'hosts',
    'process': 'processes',
    'application': 'processes',
}


XML_ATTRS_TO_YAML = {
    'bootstraptime': 'bootstrap_end_time',
    'starttime': 'start_time',
    'stoptime': 'stop_time',
    'arguments': 'args',
    'iphint': 'ip_address_hint',
    'citycodehint': 'city_code_hint',
    'countrycodehint': 'country_code_hint',
    'geocodehint': 'geo_code_hint',
    'typehint': 'type_hint',
    'loglevel': 'log_level',
    'heartbeatloglevel': 'heartbeat_log_level',
    'heartbeatloginfo': 'heartbeat_log_info',
    'pcapdir': 'pcap_directory',
    'bandwidthdown': 'bandwidth_down',
    'bandwidthup': 'bandwidth_up',
    'heartbeatfrequency': 'heartbeat_interval',
    'socketrecvbuffer': 'socket_recv_buffer',
    'socketsendbuffer': 'socket_send_buffer',
    'interfacebuffer': 'interface_buffer',
}


def convert_xml_tag_to_yaml_key(tag: str) -> str:
    '''
    Convert an XML tag in its YAML key
    '''
    return XML_TAGS_TO_YAML.get(tag, tag)


def convert_xml_attr_to_yaml_key(tag: str) -> str:
    '''
    Convert an XML attribute in its YAML key
    '''
    return XML_ATTRS_TO_YAML.get(tag, tag)


def yaml_str_presenter(dumper, data):
    '''
    Convert a string YAML representation to be more human friendly
    '''
    if len(data.splitlines()) > 1:  # check for multiline string
        # note: pyyaml will use quoted style instead of the literal block style if the string
        # contains trailing whitespace: https://github.com/yaml/pyyaml/issues/121
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
        k = convert_xml_attr_to_yaml_key(k)
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
        rv = {
            **convert_integer(node.attrib)
        }

        if node.text is not None:
            rv['graphml'] = node.text.strip()

        return rv

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
        converted = {
            'general': options,
            **xml_nodes_to_dict(root)
        }
    else:
        converted = {
            **xml_nodes_to_dict(root)
        }

    shadow_dict_post_processing(converted)

    return converted


def append_ld_preload(env_str, preload_path):
    '''
    Append a path to LD_PRELOAD, which may or may not exist in the environment string.
    '''

    if len(env_str) != 0:
        env_vars = env_str.split(';')
    else:
        env_vars = []

    env_vars = [x.split('=', 1) for x in env_vars]

    for x in env_vars:
        if x[0] == 'LD_PRELOAD':
            paths = x[1].split(':')
            paths.insert(0, preload_path)
            x[1] = ':'.join(paths)
            break
    else:
        env_vars.append(['LD_PRELOAD', preload_path])

    env_vars = ['='.join(x) for x in env_vars]
    return ';'.join(env_vars)


def shadow_dict_post_processing(shadow: Dict):
    '''
    Remove deprecated fields and perform other adjustments (remove the 'plugins' list,
    convert the hosts from a list to a dict, etc)
    '''

    # the 'environment' attribute is now per-process
    saved_environment = None

    if 'general' in shadow:
        if 'preload' in shadow['general']:
            # we no longer support the 'preload' attribute
            removed = shadow['general'].pop('preload')
            print_deprecation_msg('preload', removed)
        if 'environment' in shadow['general']:
            saved_environment = shadow['general'].pop('environment')

    # save the list of plugins
    plugins = {}
    if 'plugins' in shadow:
        for plugin in shadow['plugins']:
            plugins[plugin['id']] = plugin['path']
            del plugin['id']
            del plugin['path']
            # check for remaining attributes (for example 'startsymbol')
            for remaining in plugin:
                print_deprecation_msg(remaining, plugin[remaining])
        del shadow['plugins']

    if 'hosts' in shadow:
        # switch the hosts list to a dictionary
        num_hosts = len(shadow['hosts'])
        shadow['hosts'] = {x['id']: {y: x[y] for y in x if y != 'id'} for x in shadow['hosts']}
        assert num_hosts == len(shadow['hosts']), "Invalid input: there are hosts with duplicate ids"

        for host_name in shadow['hosts']:
            host = shadow['hosts'][host_name]

            # add all extra fields to an 'options' field
            host_non_option_names = ['quantity', 'processes', 'bandwidth_down', 'bandwidth_up']
            host_options = {x: host[x] for x in host if x not in host_non_option_names}
            host_non_options = {x: host[x] for x in host if x in host_non_option_names}

            host = host_non_options
            if len(host_options) != 0:
                host['options'] = host_options
            shadow['hosts'][host_name] = host

            # shadow classic uses bandwidth units of KiB/s
            if 'bandwidth_up' in host:
                host['bandwidth_up'] = str(host['bandwidth_up'] * 8) + ' Kibit'

            if 'bandwidth_down' in host:
                host['bandwidth_down'] = str(host['bandwidth_down'] * 8) + ' Kibit'

            if 'options' in host:
                # shadow classic automatically disables autotuning if the buffer sizes are set
                if 'socket_send_buffer' in host['options']:
                    host['options']['socket_send_autotune'] = False

                if 'socket_recv_buffer' in host['options']:
                    host['options']['socket_recv_autotune'] = False

            for process in host['processes']:
                # replace the plugin name with its path
                plugin = process.pop('plugin')
                process['path'] = plugins[plugin]

                if saved_environment is not None:
                    # add the saved environment
                    assert 'environment' not in process, "Process should not have an environment attribute"
                    process['environment'] = saved_environment

                if 'preload' in process:
                    # we no longer support the 'preload' attribute, so append it to LD_PRELOAD
                    preload = process.pop('preload')
                    preload_path = os.path.expanduser(plugins[preload])

                    if 'environment' not in process:
                        process['environment'] = ''
                    process['environment'] = append_ld_preload(process['environment'], preload_path)

    if 'network' in shadow:
        assert len(shadow['network']) == 1, "Invalid input: there is more than one topology"
        shadow['network'] = shadow['network'][0]

        path = None
        gml = None

        if 'path' in shadow['network']:
            path = shadow['network'].pop('path')
            print("External topology file '{}' was not converted".format(path), file=sys.stderr)

        if 'graphml' in shadow['network']:
            graphml = shadow['network'].pop('graphml')
            tree = ET.ElementTree(ET.fromstring(graphml))
            new_topology = io.BytesIO()
            removed_graph_data = convert_topology(tree.getroot(), new_topology, False)
            new_topology.seek(0)
            gml = new_topology.read().decode("utf-8")

            if 'preferdirectpaths' in removed_graph_data:
                value = removed_graph_data.pop('preferdirectpaths')
                value = True if value.lower() == 'true' else False
                shadow['network']['use_shortest_path'] = not value

            for removed in removed_graph_data:
                print_deprecation_msg(removed, removed_graph_data[removed])

        shadow['network']['graph'] = {}
        shadow['network']['graph']['type'] = 'gml'

        if path is not None:
            shadow['network']['graph']['path'] = path

        if gml is not None:
            shadow['network']['graph']['inline'] = gml


def print_deprecation_msg(field: str, value: str):
    print("Removed deprecated attribute '{}': '{}'".format(field, value), file=sys.stderr)


def get_xml_root_from_filename(filename: str) -> ET.Element:
    '''
    Load an XML Element from a XML file
    '''
    tree = ET.parse(filename)
    return tree.getroot()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert shadow config files from XML to YAML')
    parser.add_argument('filename', help='Filename to convert')
    parser.add_argument('--output', help='Output filename', default=None, nargs='?')
    args = parser.parse_args()

    xml_root = get_xml_root_from_filename(args.filename)
    d = shadow_xml_to_dict(xml_root)
    with get_output_stream(args, 'xml', 'yaml') as stream:
        save_dict_in_yaml_file(d, stream)
