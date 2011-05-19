#!/usr/bin/python

import sys

if len(sys.argv) != 3:
	print "Usage: " + sys.argv[0] + "output_from_nm_command path/to/var_registration.c.hint"
	exiterror()

gvars = open(sys.argv[1]).readlines()
svars = open(sys.argv[2]).readlines()

print "# the following variables are not found in var_registration.c.hint and should be registered:"
for g in gvars:
	g = g.strip()
	found = 0
	for s in svars:
		if s.find(g) > -1: found = 1
	if found == 0:
		print "sizeof(" + g + "), &" + g + ","
