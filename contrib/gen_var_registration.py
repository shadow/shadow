#!/usr/bin/python
"""
This script takes the var_definitions.c file that is generated when compiling Tor
with the --disable-static-vars flag, and parses the file for static variables.
The output is a file that contains the static variables in a generated
register_globals method for use inside a DVN module.

NOTE: This only gets variables that are properly parsed in Tor, and only statics.
Therefore its important to run nm on the actual libraries being included in the
Tor module to ensure all variables are registered properly. The nm command is 
something like:
nm -f sysv --defined-only test/src/or/tor | grep OBJECT | grep -v __func__ | grep -v __PRETTY_FUNCTION__ | cut -d'|' -f1,3

You can use the script "find_unregistered_vars.py" to take the output of nm, 
and find which varaibles were not included in the file that THIS script generates.
"""

import sys

def main():
    if len(sys.argv) != 2:
        print sys.argv[0] + ": wrong number of args"
        print "Usage: " + sys.argv[0] + "path/to/var_definitions.c"
        exiterror()
        
    # extract our information from the tor variable file
    filename = sys.argv[1]
    includes, variables = extract(filename)
    
    varstring, inclstring, regstring = "", "", ""
    num_vars = 2 # for vtor and errno
    for i in includes:
        inclstring += i
    inclstring += "\n"
    for v in variables:
        varstring += "extern " + v
    for v in variables:
        s = get_reg_str(v)
        if s != "":
            regstring += s
            num_vars += 1
    
    header = "\
#define _GNU_SOURCE 1\n\
#include <sys/types.h>\n\
\n\
# ifndef __daddr_t_defined\n\
typedef __daddr_t daddr_t;\n\
typedef __caddr_t caddr_t;\n\
#  define __daddr_t_defined\n\
# ifndef __u_char_defined\n\
# endif\n\
typedef __u_char u_char;\n\
typedef __u_short u_short;\n\
typedef __u_int u_int;\n\
typedef __u_long u_long;\n\
typedef __quad_t quad_t;\n\
typedef __u_quad_t u_quad_t;\n\
typedef __fsid_t fsid_t;\n\
#  define __u_char_defined\n\
# endif\n\
const char tor_git_revision[] = \"\";\n\
\n\
#include <errno.h>\n\
\n\
#include \"vtor.h\"\n\
#include \"snri.h\"\n\
\n\
"

    body = "\
\n\
void vtor_register_globals(vtor_tp vtor) {\n\
\tsnri_register_globals(" + str(num_vars) + ",\n\
\t\tsizeof(vtor_t), vtor,\n\
"
    
    footer = "\
\t\tsizeof(errno), &errno\n\
\t\t);\n\
}\n\
"
    
    # write the variable registration file
    outf = open("var_registration.c.hint", 'w')
    outf.write(header)
    outf.write(inclstring)
    outf.write(varstring)
    outf.write(body)
    outf.write(regstring)
    outf.write(footer)
    outf.close()

def extract(filename):
    inf = open(filename, 'r')
    lines = inf.readlines()
    inf.close()
    
    incls, vars = [], []
    i = 0
    while i < len(lines):
        val = ""
        line = lines[i]
        if line.find("#include") > -1: incls.append(line)
        elif line.find("#ifdef ENABLE_BUF_FREELISTS") > -1: #skip this
            while lines[i].find("#endif") < 0:
                i += 1
        elif line[0:1] == " " or line[0:2] == "};": pass
        elif line.find(";") < 0 and line.find("[]") > -1:
            # count the array elements so we can extern and size properly
            # get the definition
            arrline = ""
            while lines[i].find("};") < 0:
                arrline += lines[i]
                i += 1
            arrline += lines[i]
            # now count commas that are not enclosed in () or {}
            nitems, nbraces, nparens = 1, 0, 0 #last item may or may not be followed by comma, add 1 to be safe
            for char in arrline:
                if char == "{": nbraces += 1
                elif char == "}": nbraces -= 1
                elif char == "(": nparens += 1
                elif char == ")": nparens -= 1
                elif char == "," and nparens == 0 and nbraces == 1:
                    nitems += 1
            if nbraces != 0 or nparens != 0: exiterror()
            #now get the variable
            val = line[:line.index("[")+1]
            val += str(nitems) + "]"
        else:
            j = line.find("=")
            if j > -1: val = line[:j]
            else:
                j = line.find(";")
                if j > -1: val = line[:j]
                else: pass # must be line without variable name
        if val != "":
            vars.append(val.strip().replace("\t"," ") + ";\n")
        i += 1

    return incls, vars

'''
Here is a sample of the types of defs we are working with:
extern tor_mutex_t **_openssl_mutexes;
extern BIGNUM *dh_param_p;
extern STACK_OF(SSL_CIPHER) *CLIENT_CIPHER_STACK;
extern struct pdinfo *last_dir;
extern uint16_t (*trans_id_function)(void);
extern int gzip_is_supported;
extern struct tlsmap tlsmap_root;
extern unsigned int    malloc_started;
extern char ssl_state_buf[40];
'''
def get_reg_str(v):
    type, name = "", ""
    if v.find("*") > -1: type, name = get_reg_str_pointer(v)
    elif v.find("[") > -1: type, name = get_reg_str_array(v)
    else: type, name = get_reg_str_normal(v)

    #blacklist overrides
    return "\t\tsizeof(" + type + "), " + name + ",\n"

def get_reg_str_pointer(v):
    type = "char*"
    name = ""
    if v.count("(") > 1:
        name = v[v.index("*")+1:v.index(")")]
    elif v.find("**") > -1:
        name = v[v.index("**")+2:v.rindex(";")]
    else:
        name = v[v.index("*")+1:v.rindex(";")]
    return type, "&" + name

def get_reg_str_array(v):
    parts = v.split()
    t = parts[len(parts)-1]
    name = str(t[:t.index("[")])
    type = name
    return type, name

def get_reg_str_normal(v):
    v = v.replace("\t","")
    type = v[:v.rindex(" ")]
    name = v[v.rindex(" ")+1:v.rindex(";")]
    return type, "&" + name

def exiterror():
    print "Error generating var registration"
    exit(-1)
    
def exitsuccess():
    exit(0)
     
if __name__ == '__main__':
    main()
    exitsuccess()