#!/usr/bin/env python3

"""\
Reads the output of bindgen on `stdin`, and writes a mangled version to `stdout`.

In particular this is meant to be run on the output of running `bindgen` over
the Linux kernel headers. Types, constant names, and field names are prefixed
with `linux_`, `LINUX_`, or `l_`. This mangling makes the output safe to be
exported back out to C via cbindgen without causing conflicts with libc or
kernel headers.
"""

import re
import sys

def maybe_underbar(s):
    if s[0] == '_':
        return ''
    else:
        return '_'

def get_types(s):
    ids = set()
    ids.update(re.findall(r"pub type (\w+)", s))
    ids.update(re.findall(r"pub struct (\w+)", s))
    ids.update(re.findall(r"pub union (\w+)", s))
    ids.update(re.findall(r"pub enum (\w+)", s))
    return ids
    
def prefix_types(ids, s):
    for t in ids:
        s = re.sub(f'\\b{t}\\b', f'linux{maybe_underbar(t)}{t}', s)
    return s

def get_consts(s):
    return re.findall(r"pub const (\w+)", s)
    
def prefix_consts(ids, s):
    for t in ids:
        s = re.sub(f'\\b{t}\\b', f'LINUX{maybe_underbar(t)}{t}', s)
    return s

def get_field_names(s):
    # We have to to do field names too, since they
    # conflict with macros in libc.
    return re.findall(r"pub (\w+): ", s)

def prefix_field_names(ids, s):
    for t in ids:
        s = re.sub(f'\\b{t}\\b', f'l{t}', s)
    return s

def mangle(s):
    types = get_types(s)
    s = prefix_types(types , s)

    consts = get_consts(s)
    s = prefix_consts(consts, s)

    field_names = get_field_names(s)
    s = prefix_field_names(field_names, s)

    return s

if __name__ == '__main__':
    sys.stdout.write(mangle(sys.stdin.read()))
