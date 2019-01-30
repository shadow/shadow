<shadow>
#!/usr/bin/python

import sys
from lxml import etree

def main():
    # read the existing config file
    tree = etree.parse("shadow.config.xml")
    root = tree.getroot()

    # add new attributes to the <shadow> element
    root.set("preload", "~/.shadow/lib/libshadow-interpose.so")
    root.set("environment", "OPENSSL_ia32cap=~0x200000200000000;EVENT_NOSELECT=1;EVENT_NOPOLL=1;EVENT_NOKQUEUE=1;EVENT_NODEVPOLL=1;EVENT_NOEVPORT=1;EVENT_NOWIN32=1")

    # add tor-preload as a plugin
    torpreload = etree.Element("plugin")
    torpreload.set("id", "tor-preload")
    torpreload.set("path", "~/.shadow/lib/libshadow-preload-tor.so")
    root.insert(2, torpreload) # insert after the topology element

    # plugin paths need to refer to lib dir, not plugin dir
    for plugin in root.iter('plugin'):
        plugin.set("path", plugin.attrib['path'].replace(".shadow/plugins/", ".shadow/lib/"))

    # set the preload library for all tor applications
    for node in root.iter('node'):
        for app in node.iter('application'):
            if app.attrib['plugin'] == 'tor':
                app.set("preload", "tor-preload")

    # save the new config file
    with open("new.shadow.config.xml", 'wb') as f:
        print >>f, etree.tostring(root, pretty_print=True, xml_declaration=False)

if __name__ == '__main__':
    main()