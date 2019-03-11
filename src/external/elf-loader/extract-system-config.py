#!/usr/bin/env python

import sys
import re
import getopt
import os
import codecs
from elftools.elf.elffile import ELFFile

class CouldNotFindFile:
    pass

class DebugData:
    def __init__(self, debug_filename):
        with open(debug_filename, 'rb') as f:
            elffile = ELFFile(f)
            assert elffile.has_dwarf_info(), debug_filename + ' has no DWARF info'
            self.dwarfinfo = elffile.get_dwarf_info()
            return
    def get_struct_size(self, struct_die, required=True):
        if struct_die is not None:
            return struct_die.attributes['DW_AT_byte_size'].value
        assert not required
        return None
    def get_member_offset (self, die, member_name, required=True):
        member = self._find_member_in_struct(die, member_name.encode('UTF-8'))
        if member is not None and 'DW_AT_data_member_location' in member.attributes:
            return member.attributes['DW_AT_data_member_location'].value
        assert not required
        return None
    def get_struct_die(self, struct_name):
        return self._get_X('DW_TAG_structure_type', struct_name)
    def get_type_die(self, type_name):
        typedef = self._get_X('DW_TAG_typedef', type_name)
        ref = typedef.attributes['DW_AT_type'].value
        for CU in self.dwarfinfo.iter_CUs():
            for die in CU.iter_DIEs():
                if die.offset == ref:
                    return die
        return None
    def _find_in_DIE(self, die, tag_name, struct_name):
        if die.tag == tag_name and \
           'DW_AT_name' in die.attributes and \
           die.attributes['DW_AT_name'].value == struct_name:
            return die
        if die.has_children:
            for child in die.iter_children():
                result = self._find_in_DIE(child, tag_name, struct_name)
                if result is not None:
                    return result
        return None
    def _get_X(self, tag_name, item_name):
        for CU in self.dwarfinfo.iter_CUs():
            for die in CU.iter_DIEs():
                item = self._find_in_DIE(die, tag_name, item_name.encode('UTF-8'))
                if item is not None:
                    return item
        return None
    def _find_member_in_struct(self, struct, member_name):
        for die in struct.iter_children():
            if die.tag == 'DW_TAG_member' and \
               'DW_AT_name' in die.attributes and \
               die.attributes['DW_AT_name'].value == member_name:
                return die
        return None

def find_build_id(path):
    if not os.path.exists(path):
        return None
    with open(path, 'rb') as f:
        elffile = ELFFile(f)
        section = elffile.get_section_by_name('.note.gnu.build-id')
        build_id = ''
        try:
            note = next(section.iter_notes())
            build_id = note['n_desc']
        except AttributeError:
            # older versions of pyelftools don't support notes,
            # so parse the section data directly
            build_id = codecs.getencoder('hex')(section.data()[-20:])[0].decode('UTF-8')
        return "/usr/lib/debug/.build-id/{}/{}.debug".format(build_id[0:2], build_id[2:])

def check_file_regex(directory, file_regex):
    if not os.path.exists(directory):
        return None
    lines = os.listdir(directory)
    regex = re.compile(file_regex)
    for line in lines:
        result = regex.search(line)
        if result:
            return directory + result.group()
    return None

def search_debug_file():
    debug_files = [ ('/usr/lib64/debug/lib64/', r'ld-[0-9.]+\.so.debug'),
                    ('/usr/lib/debug/lib64/', r'ld-linux-x86-64\.so\.2\.debug'),
                    ('/usr/lib/debug/', r'ld-linux-x86-64\.so\.2'),
                    ('/usr/lib/debug/lib/', r'ld-linux\.so\.2\.debug'),
                    ('/usr/lib/debug/', r'ld-linux\.so\.2'),
                    # ubuntu 09.10-10.10
                    ('/usr/lib/debug/lib/', r'ld-[0-9.]+\.so'),
                    # ubuntu 11.04/11.10
                    ('/usr/lib/debug/lib/i386-linux-gnu/', r'ld-[0-9.]+\.so'),
                    ('/usr/lib/debug/lib/x86_64-linux-gnu/', r'ld-[0-9.]+\.so'),
                    # ubuntu >12.04
                    ('/usr/lib/debug/lib/x86_64-linux-gnu/', r'ld-[0-9.]+\.so'),
                    ('/usr/lib/debug/lib/i386-linux-gnu/', r'ld-[0-9.]+\.so'),
                  ]
    build_ids = [ # debian
                  ('/lib/x86_64-linux-gnu/', r'ld-[0-9.]+\.so'),
                  ('/lib/i386-linux-gnu/', r'ld-[0-9.]+\.so'),
                  # solus
                  ('/usr/lib/', r'ld-linux-x86-64\.so\.2'),
                ]
    for file_tuple in debug_files:
        file = check_file_regex(file_tuple[0], file_tuple[1])
        if file and os.path.isfile(file):
            return file
    for file_tuple in build_ids:
        library = check_file_regex(file_tuple[0], file_tuple[1])
        if not library:
            continue
        file = find_build_id(library)
        if file and os.path.isfile(file):
            return file
    raise CouldNotFindFile()

def list_lib_path():
    paths = []
    re_lib = re.compile ('(?<=^#)')
    if not os.path.isdir("/etc/ld.so.conf.d/"):
        return ''
    for filename in os.listdir("/etc/ld.so.conf.d/"):
        try:
            for line in open("/etc/ld.so.conf.d/" + filename, 'r'):
                if re_lib.search(line) is not None:
                    continue
                paths.append(line.rstrip())
        except:
            continue
    return ':'.join(paths)

def define(outfile, name, value):
    if value is not None:
        outfile.write('#define {} {}\n'.format(name, value))

def usage():
    print('''Usage: ./extract-system-config.py [OPTIONS]
Options:
\t-h, --help\tdisplay this help text
\t-c, --config=[FILE]\twrite output to file (default: stdout)
\t-d, --debug=[FILE]\tread debug symbols from file (default: search common locations)
\t-b, --builddir=[DIR]\tbuild directory for inclusion in library path''')

def main(argv):
    config_filename = ''
    debug_filename = ''
    build_dir = ''
    try:
        opts, args = getopt.getopt(argv, 'hc:d:b:',
                                   ['help', 'config=', 'debug=', 'builddir='])
    except getopt.GetoptError:
        usage()
        sys.exit(2)
    for opt, arg in opts:
        if opt in ('-h', '--help'):
            usage()
            sys.exit()
        elif opt in ('-c', '--config'):
            config_filename = arg
        elif opt in ('-d', '--debug'):
            debug_filename = arg
        elif opt in ('-b', '--builddir'):
            build_dir = arg

    if config_filename != '':
        config = open(config_filename, 'w')
    else:
        config = sys.stdout
    if debug_filename == '':
        debug_filename = search_debug_file()
    find_build_id(debug_filename)
    debug = DebugData(debug_filename)
    rtld_global_die = debug.get_struct_die('rtld_global')
    rtld_global_ro_die = debug.get_struct_die('rtld_global_ro')
    tcb_die = debug.get_type_die('tcbhead_t')
    define(config, 'CONFIG_RTLD_GLOBAL_SIZE', debug.get_struct_size(rtld_global_die))
    # field was removed in glibc 2.25
    define(config, 'CONFIG_DL_ERROR_CATCH_TSD_OFFSET', debug.get_member_offset (rtld_global_die, '_dl_error_catch_tsd', False))
    define(config, 'CONFIG_RTLD_GLOBAL_RO_SIZE', debug.get_struct_size(rtld_global_ro_die))
    define(config, 'CONFIG_RTLD_DL_PAGESIZE_OFFSET', debug.get_member_offset(rtld_global_ro_die, '_dl_pagesize'))
    define(config, 'CONFIG_RTLD_DL_CLKTCK_OFFSET', debug.get_member_offset(rtld_global_ro_die, '_dl_clktck'))
    define(config, 'CONFIG_TCB_SIZE', debug.get_struct_size(debug.get_struct_die('pthread')))
    define(config, 'CONFIG_TCB_TCB_OFFSET', debug.get_member_offset(tcb_die, 'tcb'))
    define(config, 'CONFIG_TCB_DTV_OFFSET', debug.get_member_offset(tcb_die, 'dtv'))
    define(config, 'CONFIG_TCB_SELF_OFFSET',debug.get_member_offset(tcb_die, 'self'))
    define(config, 'CONFIG_TCB_SYSINFO_OFFSET', debug.get_member_offset(tcb_die, 'sysinfo'))
    define(config, 'CONFIG_TCB_STACK_GUARD', debug.get_member_offset(tcb_die, 'stack_guard'))
    define(config, 'CONFIG_SYSTEM_LDSO_LIBRARY_PATH', '"' + list_lib_path() + ':' + build_dir + '\"')

if __name__ == "__main__":
    main(sys.argv[1:])
