#!/usr/bin/python

##
# Scallion - plug-in for The Shadow Simulator
#
# Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
#
# This file is part of Scallion.
#
# Scallion is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Scallion is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Scallion.  If not, see <http://www.gnu.org/licenses/>.
#

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
