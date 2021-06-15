#!/usr/bin/env python3

from __future__ import print_function
import os,sys
from lxml import etree


def main():
    if len(sys.argv) < 3:
        print('Usage: {0} <old hosts filename> <new hosts filename>'.format(sys.argv[0]))
        sys.exit(0)

    # mappings from scallion types to plugins
    scallion_plugins = {'client': 'filetransfer', 'torrent': 'torrent'}

    tree = etree.parse(sys.argv[1])
    root = tree.getroot()

    # create kill element
    kill = root.find('kill')

    # create plugin elements
    plugins = root.iter('plugin')

    # get software elements
    software = {}
    for element in root.iter('software'):
        software[element.attrib['id']] = element

    nodes = []
    for element in root.iter('node'):
        node = etree.Element('node')
        if 'id' in element.attrib: 
            node.set('id', element.attrib['id'])
        if 'cluster' in element.attrib:
            node.set('cluster', element.attrib['cluster'])
        if 'bandwidthdown' in element.attrib:
            node.set('bandwidthdown', element.attrib['bandwidthdown'])
        if 'bandwidthup' in element.attrib:
            node.set('bandwidthup', element.attrib['bandwidthup'])
        if 'quantity' in element.attrib:
            node.set('quantity', element.attrib['quantity'])
        if 'cpufrequency' in element.attrib:
            node.set('cpufrequency', element.attrib['cpufrequency'])

        sw = software[element.attrib['software']]
        application = etree.SubElement(node, 'application')
        application.set('plugin', sw.attrib['plugin'])
        application.set('time', sw.attrib['time'])
        application.set('arguments', sw.attrib['arguments'])

        arguments = sw.attrib['arguments'].split(' ')
        if sw.attrib['plugin'] == 'scallion' and len(arguments) > 7:
            application.set('arguments', ' '.join(arguments[0:7]))

            application = etree.SubElement(node, 'application')
            if arguments[7] in scallion_plugins:
                application.set('plugin', scallion_plugins[arguments[7]])
            else:
                application.set('plugin', arguments[7])
            application.set('time', str(int(sw.attrib['time']) + 600))
            application.set('arguments', ' '.join(arguments[7:len(arguments)]))

        nodes.append(node)


    fout = open(sys.argv[2], 'w')
    fout.write(etree.tostring(kill, pretty_print=True, xml_declaration=False))

    for plugin in plugins:
        fout.write(etree.tostring(plugin, pretty_print=True, xml_declaration=False))
    fout.write('\n')

    for node in nodes:
        fout.write(etree.tostring(node, pretty_print=True, xml_declaration=False))
        fout.write('\n')
    fout.close()


if __name__ == '__main__':
    main()
