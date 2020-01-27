#!/usr/bin/env python
import sys
import subprocess
import re

try:
    version = subprocess.Popen (['valgrind', '--version'],
                                stdout = subprocess.PIPE,
                                stderr = subprocess.PIPE)
except Exception:
    sys.exit(1)

ver_re = re.compile('([0-9]+)\.([0-9]+)\.([0-9]+)')
for l in version.stdout.readlines():
    m = ver_re.search (l)
    if m is None:
        continue
    ver = int(m.group(1)) * 100 + int(m.group(2)) * 10 + int(m.group(3))
    break

cmd = ['valgrind',
       '--leak-check=full',
       '--show-reachable=yes',
       '--error-exitcode=2']
if ver >= 340:
    cmd += ['--track-origins=yes']

cmd += sys.argv[1:]

val = subprocess.Popen(cmd,
                       stdout = subprocess.PIPE,
                       stderr = subprocess.PIPE)
(stdout, stderr) = val.communicate()
if val.returncode != 0 or "== LEAK SUMMARY:" in stderr:
    sys.exit(1)
