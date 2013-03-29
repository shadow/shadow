#!/usr/bin/python2.7

import os,sys,re,subprocess,shlex

def main():
    scallion_registration = open('../../src/plugins/scallion/scallion-registration.c', 'w')
    tor_externs = open('../../src/plugins/scallion/tor_externs.h', 'w')

    create_scallion_registration(scallion_registration)
    tor_externs.write('#include "tor_includes.h"\n\n')

    vars_converted = {}
    vars_global = {}

    directories = ['src/common', 'src/or']

    # List of variables to skip when adding to tor_externs.h.  This is
    # because these variables are declared in header files which get included.
    vars_to_skip = ['_log_global_min_severity']

    # List of functions that need to be globalized for scallion plugin
    function_list = [
            'socket_accounting_lock', 
            'socket_accounting_unlock', 
            'tor_libevent_get_base',
            'tor_cleanup',
            'second_elapsed_callback',
            'refill_callback',
            'identity_key_is_set',
            'client_identity_key_is_set',
            'init_keys',
            'init_cell_pool',
            'connection_bucket_init',
            'trusted_dirs_reload_certs',
            'router_reload_router_list',
            'directory_info_has_arrived',
            'tor_init',
            'logv',
            'tor_open_socket',
            'tor_gettimeofday',
            'spawn_func',
            'rep_hist_bandwidth_assess']

    # List of functions that need to be renamed
    functions_to_rename = {
            'client_identity_key_is_set' : 'identity_key_is_set'
            }

    # Variable types that need to be explicit for src/vtor.c
    var_types = {
            'stats_prev_global_read_bucket' : 'int',
            'stats_prev_global_write_bucket' : 'int',
            'global_read_bucket' : 'int',
            'global_write_bucket' : 'int',
            'second_timer' : 'periodic_timer_t *',
            'refill_timer' : 'periodic_timer_t *',
            'client_identitykey' : 'crypto_pk_t *',
            'active_linked_connection_lst' : 'smartlist_t *',
            'called_loop_once' : 'int',
            'n_sockets_open' : 'int'}

    # Go through all the files in the directories listed and globalize symbols
    for directory in directories:
        files = os.listdir(directory)
        for filename in files:
            if filename.endswith('.o'): 
                filename = directory + '/' + filename
                print "{0}".format(filename)

                # Run objdump and get symbol list in .data  .bss  .text and *COM* secionts
                cmd = "objdump -j .data -j .bss -j *COM* -j .text -j *UND* -t " + filename
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()
                objdump = output.split('\n')

                # Run nm to get list of symbol sizes for scallion_registration.c
                cmd = "nm --print-size --size-sort " + filename
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()
                sym_sizes = output.split('\n')

                # Files to hold symbols that need to be renamed and globalized
                sym_rename_file = open('sym_rename', 'w')
                sym_globalize_file = open('sym_globalize', 'w')

		file_format = objdump[1]
                # Remove the first 4 and last 3 lines, these don't contain symbol information
                objdump = objdump[4:len(objdump)-3]
                for line in objdump:
                    symbol_name = line.rsplit(' ',1)[1]
                    symbol_type = line[15] if file_format.find("elf32") > -1 else line[23]
                    #print "found sym: {0}, type: {1}".format(symbol_name, symbol_type)
                    # Check to make sure symbol is an object
                    if symbol_type == 'O':
                        variable = symbol_name
                        variable_orig = variable

                        # If the variable name ends in .##### it's static and needs to be renamed
                        m = re.search('\.[0-9]+$', variable)
                        if m != None:
                            if variable in vars_converted:
                                variable = vars_converted[variable]
                            else:
                                # Get the variable from the end of the line
                                variable = variable_orig.split('.')[0]
                                counter = 1
                                # Go through incrementing the counter to add to the symbol name until
                                # a name that's not being globalized is found
                                while variable in vars_global.keys():
                                    variable = variable_orig.split('.')[0] + str(counter)
                                    counter = counter + 1
                                
                                vars_converted[variable_orig] = variable

                            sym_rename_file.write('{0} {1}\n'.format(variable_orig, variable))

                        sym_globalize_file.write('{0}\n'.format(variable))

                        # Go through nm output and find the symbol size
                        for line in sym_sizes:
                            line = line.split(' ')
                            if len(line) == 4 and line[3] == variable_orig:
                                size = line[1].lstrip('0')
                                vars_global[variable] = size
                                break

                        if variable not in vars_global.keys():
                            print '\tWARNING: Could not find variable {0} in nm output for file {1}'.format(variable, filename)

                    else:
                        if symbol_name in functions_to_rename.keys():
                            sym_rename_file.write('{0} {1}\n'.format(symbol_name, functions_to_rename[symbol_name]))
                            symbol_name = functions_to_rename[symbol_name]
                        if symbol_name in function_list:
                            sym_globalize_file.write('{0}\n'.format(symbol_name))


                sym_rename_file.close()
                sym_globalize_file.close()


                # Rename all symbols that were duplicated
                cmd = 'objcopy ' + filename + ' --redefine-syms=' + sym_rename_file.name
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()
             
                # Globalize all symbols found in the objdump output
                cmd = 'objcopy ' + filename + ' --globalize-symbols=' + sym_globalize_file.name
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()


    # Remove the temporary files used to rename and global symbols
    os.remove(sym_rename_file.name)
    os.remove(sym_globalize_file.name)

    scallion_registration.write('\tscallionData->shadowlibFuncs->registerPlugin(scallionFuncs, {0},\n'.format(len(vars_global.keys()) + 1))
    scallion_registration.write('\t\t(gsize) sizeof(Scallion), (gpointer) scallionData')
    
    for var in vars_global:
        scallion_registration.write(',\n\t\t(gsize) 0x{0}, (gpointer) &{1}'.format(vars_global[var], var))
        if var not in vars_to_skip: 
            if var in var_types.keys():
                tor_externs.write('extern {1} {0};\n'.format(var, var_types[var]))
            else:
                tor_externs.write('extern uint8_t {0}[{1}];\n'.format(var, int(vars_global[var], 16)))


    scallion_registration.write('\n')
    scallion_registration.write('\n\t);\n')
    scallion_registration.write('}\n')

    scallion_registration.close()
    tor_externs.close()

def create_scallion_registration(scallion_registration):
    scallion_registration.write('/**\n')
    scallion_registration.write(' * Scallion - plug-in for The Shadow Simulator\n')
    scallion_registration.write(' *\n')
    scallion_registration.write(' * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>\n')
    scallion_registration.write(' *\n')
    scallion_registration.write(' * This file is part of Scallion.\n')
    scallion_registration.write(' *\n')
    scallion_registration.write(' * Scallion is free software: you can redistribute it and/or modify\n')
    scallion_registration.write(' * it under the terms of the GNU General Public License as published by\n')
    scallion_registration.write(' * the Free Software Foundation, either version 3 of the License, or\n')
    scallion_registration.write(' * (at your option) any later version.\n')
    scallion_registration.write(' *\n')
    scallion_registration.write(' * Scallion is distributed in the hope that it will be useful,\n')
    scallion_registration.write(' * but WITHOUT ANY WARRANTY; without even the implied warranty of\n')
    scallion_registration.write(' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n')
    scallion_registration.write(' * GNU General Public License for more details.\n')
    scallion_registration.write(' *\n')
    scallion_registration.write(' * You should have received a copy of the GNU General Public License\n')
    scallion_registration.write(' * along with Scallion.  If not, see <http://www.gnu.org/licenses/>.\n')
    scallion_registration.write(' *\n');
    scallion_registration.write(' * NOTE: THIS FILE AUTOMATICALLY GENERATED BY SETUP SCRIPT!\n')
    scallion_registration.write(' */\n')
    scallion_registration.write('\n')
    scallion_registration.write('const char tor_git_revision[] = "";\n')
    scallion_registration.write('\n')
    scallion_registration.write('#include "scallion.h"\n')
    scallion_registration.write('\n')
    scallion_registration.write('#include "tor_includes.h"\n')
    scallion_registration.write('#include "tor_externs.h"\n')
    scallion_registration.write('\n')
    scallion_registration.write('void scallion_register_globals(PluginFunctionTable* scallionFuncs, Scallion* scallionData) {\n')


if __name__ == '__main__':
    main()
