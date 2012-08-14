#!/usr/bin/python

# takes in an alexa top 1 million csv, cuts it to top 1000, and does DNS lookups
# on those to get the IP address.
# http://s3.amazonaws.com/alexa-static/top-1m.csv.zip

import socket

def lookup(name):
    print "Looking up {0}".format(name)
    try:
        return socket.gethostbyname(name)
    except:
        return None

with open("top-1m.csv", 'rb') as fin:
    with open("alexa-top-1000-ips.csv", 'wb') as fout:
        i = 0
        for line in fin:
            item = line.strip()
            name = item.split(',')[1]
            ip = lookup(name)
            if ip is not None: 
                print >>fout, "{0},{1}".format(item, ip)
                i += 1
                if i >= 1000: break

