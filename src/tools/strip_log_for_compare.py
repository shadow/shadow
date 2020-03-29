#!/usr/bin/python

'''
Make cleaner diffs of shadow logfiles by stripping the parts of the logs that
change for every experiment (memory addresses and run timing). This is used so
we can determine if two repeated experiments produced the same results based on
their log files.
'''

from __future__ import print_function
import sys

if len(sys.argv) < 3:
    print("USAGE: {0} logfile outputfile".format(sys.argv[0]), file=sys.stderr)
    exit()

inf = open(sys.argv[1], 'r')
outf = open(sys.argv[2], 'w')

n = 0
for line in inf:
    parts = line.strip().split()
    parts = parts[1:] # skip the first timer column
    parts = [p + ' ' for p in parts if not p.startswith("0x")] # skip printing memory addresses
    parts.append("\n")
    outf.writelines(parts)
    n += 1
    
inf.close()
outf.close()

print("Done! Processed {0} lines.".format(n), file=sys.stderr)
