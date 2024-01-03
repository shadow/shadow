#!/usr/bin/env python3

"""
Reads the output of bindgen on `stdin`, and writes a mangled version to `stdout`.

In particular this is meant to be run on the output of running `bindgen` over
the Linux kernel headers. Types, constant names, and field names are prefixed
with `linux_`, `LINUX_`, or `l_`. This mangling makes the output safe to be
exported back out to C via cbindgen without causing conflicts with libc or
kernel headers.

# Caveats

The Rust code parsing here is primitive. It should be ok for the output
of bindgen, but for example wouldn't understand that a span inside
quotes is a string instead of code.
"""

import re
import sys

def get_types(s):
    """
    Return names of types declared in `s`, which is assumed to be Rust code.

    See module-level section "Caveats"
    """
    types = set()

    # From "pub type foo" extract "foo"
    types.update(re.findall(r"pub type (\w+)", s))

    # From "pub struct foo" extract "foo"
    types.update(re.findall(r"pub struct (\w+)", s))

    # From "pub union foo" extract "foo"
    types.update(re.findall(r"pub union (\w+)", s))

    # From "pub enum foo" extract "foo"
    types.update(re.findall(r"pub enum (\w+)", s))

    return types
    
def prefix_types(types, s):
    """
    Prefix each type name in `types` with `linux_` in the Rust source `s`.

    See module-level section "Caveats"
    """

    for t in types:
        # Replace "foo" with "linux_foo".
        s = re.sub(f'\\b{t}\\b', f'linux_{t}', s)
    return s

def get_consts(s):
    """
    Return names of constants declared in `s`, which is assumed to be Rust code.

    See module-level section "Caveats"
    """

    # From "pub const FOO: " extract "FOO"
    return re.findall(r"pub const (\w+): ", s)
    
def prefix_consts(consts, s):
    """
    Prefix each const name in `consts` with `LINUX_` in the Rust source `s`.

    See module-level section "Caveats"
    """
    for t in consts:
        # Replace "FOO" with "LINUX_FOO"
        s = re.sub(f'\\b{t}\\b', f'LINUX_{t}', s)
    return s

def get_field_names(s):
    """
    Return names of fields in enums and structs declared in `s`, which is
    assumed to be Rust code.

    See module-level section "Caveats"
    """
    # From "pub foo: " extract "foo"
    return re.findall(r"pub (\w+): ", s)

def prefix_field_names(fields, s):
    """
    Prefix some field names in `fields` with `l` in the Rust source `s`.

    We do this to avoid conflicts with macros that are used to access
    fields. For example, in glibc, there is a macro with the name `si_signo`
    that is meant to transparently work like a field named `si_signo` in
    `sigaction_t`.

    See module-level section "Caveats"
    """
    for t in fields:
        if (
            # siginfo fields; sometimes collide with macros in libc
            t.startswith('si_')
            # sigaction fields; sometimes collide with macros in libc
            or t.startswith('sa_')
            # libc reserves identifiers starting with _ in general.
            or t.startswith('_')
            ):
            # Replace "si_signo" with "lsi_signo"
            s = re.sub(f'\\b{t}\\b', f'l{t}', s)
    return s

def mangle(s):
    """
    Mangle the Rust source `s` to prefix identifiers.

    See module-level documentation for details.
    """

    types = get_types(s)
    s = prefix_types(types , s)

    consts = get_consts(s)
    s = prefix_consts(consts, s)

    field_names = get_field_names(s)
    s = prefix_field_names(field_names, s)

    return s

if __name__ == '__main__':
    sys.stdout.write(mangle(sys.stdin.read()))
