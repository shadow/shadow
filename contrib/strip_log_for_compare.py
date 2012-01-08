#! /usr/bin/python

'''
Make cleaner diffs of shadow logfiles by stripping the parts of the logs that
change for every experiment (memory addresses and run timing). This is used so
we can determine if two repeated experiments produced the same results based on
their log files.
'''

import sys

if len(sys.argv) < 3:
    print >>sys.stderr, "USAGE: {0} logfile outputfile".format(sys.argv[0])
    exit()

inf = open(sys.argv[1], 'rb')
outf = open(sys.argv[2], 'wb')

n = 0
for line in inf:
    parts = line.strip().split()
    for part in parts[1:]: # skip the first timer column
        if part.startswith("0x"): continue # skip printing memory addresses
        print >>outf, part,
    print >>outf, ""
    n += 1
    
inf.close()
outf.close()

print >>sys.stderr, "Done! Processed {0} lines.".format(n)
