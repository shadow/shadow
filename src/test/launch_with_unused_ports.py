#!/usr/bin/env python3

import socket
import os
import sys
import collections
import re

'''
This script calls the program in its arguments, but replaces any tags (substrings that
begin and end with '@') with a valid port number. The child program can bind to this
port as long as it sets the SO_REUSEPORT sock option. Duplicate tags will be given the
same port number.
'''

# list of sockets we're using to keep ownership of their ports
global_used_sockets = []

def get_new_port():
	'''
	Open a new inheritable socket with SO_REUSEPORT set, and return the port number. The
	socket will be saved to prevent it from being garbage collected / closed.
	'''

	s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

	# make sure the fd won't be closed when starting the child process
	s.set_inheritable(True)

	# allow the child process to bind to the same port
	s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)

	s.bind(('0.0.0.0', 0))
	port = s.getsockname()[1]

	# prevent sockets from being closed when they go out of scope
	global_used_sockets.append(s)

	return port

def replace_tags_with_ports(l):
	'''
	Replace any tags (substrings that begin and end with '@') in any list element with an
	OS-assigned port number. Duplicate tags will be given the same port number.
	'''

	assigned_ports = collections.defaultdict(get_new_port)
	replace_fn = lambda m: str(assigned_ports[m.group(0)])
	return [re.sub('@(.*?)@', replace_fn, x) for x in l]

if __name__ == '__main__':
	if len(sys.argv) == 1:
		print('(Example) {} /bin/echo A: @PORT_A@, B: @PORT_B@, A: @PORT_A@'.format(sys.argv[0]))
		print('          A: 43751, B: 46043, A: 43751')
		exit(1)

	original_argv = sys.argv[1:]
	# don't replace the program name
	replaced_argv = [original_argv[0]] + replace_tags_with_ports(original_argv[1:])

	try:
		# does not search PATH
		os.execv(replaced_argv[0], replaced_argv)
	except FileNotFoundError:
		print('Program {} was not found'.format(replaced_argv[0]))
		raise
	except PermissionError:
		print('Program {} was not executable by the current user'.format(replaced_argv[0]))
		raise
