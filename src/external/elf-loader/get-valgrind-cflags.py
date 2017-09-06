#!/usr/bin/env python

import os

if os.path.exists ('/usr/include/valgrind/valgrind.h'):
    print '-DHAVE_VALGRIND_H -I/usr/include'
