#!/usr/bin/python

# takes a xml topology file and adds a torcontrol application for each client.
# torcontrol is used in log mode to log the controller events specified herein

import sys
from lxml import etree

if len(sys.argv) != 3: print >>sys.stderr, "{0} input.xml output.xml".format(sys.argv[0]);exit()

infname = sys.argv[1]
outfname = sys.argv[2]

parser = etree.XMLParser(remove_blank_text=True)
tree = etree.parse(infname, parser)
root = tree.getroot()

p = etree.SubElement(root, "plugin")
p.set("id", "torctl")
p.set("path", "~/.shadow/plugins/libshadow-plugin-torcontrol.so")

for n in root.iterchildren("node"):
    if 'client' in n.get("id"):
        # get the time scallion starts
        starttime = None
        for a in n.iterchildren("application"):
            if 'scallion' in a.get("plugin"):
                starttime = a.get("starttime")
                if starttime is None: starttime = a.get("time")

        # create our torcontrol app 10 seconds after scallion
        if starttime is not None:
            starttime = str(int(starttime) + 10)
            a = etree.SubElement(n, "application")
            a.set("plugin", "torctl")
            a.set("starttime", starttime)
            # available events: STREAM,CIRC,ORCONN,BW,STREAM_BW,OR_BW,DIR_BW,EXIT_BW,CELL_STATS
            a.set("arguments", "single localhost 9051 log STREAM")

with open(outfname, 'wb') as outf:
    print >>outf, (etree.tostring(root, pretty_print=True, xml_declaration=False))

