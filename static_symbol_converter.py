#!/usr/bin/python

import os,sys,re,subprocess,shlex

def main():
    reg_file = open('src/scallion_registration.c', 'w')
    tor_externs = open('src/tor_externs.h', 'w')

    create_scallion_registration(reg_file)
    tor_externs.write('#include "tor_includes.h"\n\n')

    vars_converted = {}
    vars_global = {}

    tor_dir = 'build/tor-0.2.2.15-alpha/src/'
    tor_binaries = ['tor','test','tor-resolve','tor-gencert','tor-checkkey']
    directories = ['common', 'or']

    vars_to_skip = ['_log_global_min_severity']

    function_list = [
            'socket_accounting_lock', 
            'socket_accounting_unlock', 
            'tor_libevent_get_base',
            'tor_cleanup',
            'second_elapsed_callback',
            'identity_key_is_set',
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

    for directory in directories:
        files = os.listdir(tor_dir + directory)
        for filename in files:
            if filename.endswith('.o'):  #or filename.endswith('.a') or filename in tor_binaries:
                filename = tor_dir + directory + '/' + filename
                print "{0}".format(filename)

                cmd = "objdump -j .data -j .bss -j *COM* -j .text -t " + filename
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()
            
                objdump = output.split('\n')
                #objdump = objdump[4:len(objdump) - 3]

                cmd = "nm --print-size --size-sort " + filename
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()

                sym_sizes = output.split('\n')

                sym_convert_file = open('sym_convert', 'w')
                sym_globalize_file = open('sym_globalize', 'w')

                for line in objdump:
                    if len(line) > 23:
                        symbol_name = line.rsplit(' ',1)[1]
                        if line[23] == 'O':
                            variable = symbol_name
                            variable_orig = variable

                            m = re.search('\.[0-9]+$', variable)
                            if m != None:
                                if variable in vars_converted:
                                    variable = vars_converted[variable]
                                else:
                                    variable = variable_orig.split('.')[0]
                                    counter = 1
                                    while variable in vars_global.keys():
                                        variable = variable_orig.split('.')[0] + str(counter)
                                        counter = counter + 1
                                    
                                    vars_converted[variable_orig] = variable

                                sym_convert_file.write('{0} {1}\n'.format(variable_orig, variable))
                                #objcopy = 'objcopy ' + filename + ' --redefine-sym ' + variable_orig + '=' + variable
                                #p = subprocess.Popen(shlex.split(objcopy),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                                #output,error = p.communicate()


                            #objcopy = 'objcopy ' + filename + ' --globalize-symbol ' + variable
                            #p = subprocess.Popen(shlex.split(objcopy),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                            #output,error = p.communicate()
                            sym_globalize_file.write('{0}\n'.format(variable))

                            for line in sym_sizes:
                                line = line.split(' ')
                                if len(line) == 4 and line[3] == variable_orig:
                                    size = line[1].lstrip('0')
                                    vars_global[variable] = size
                                    break

                            if variable not in vars_global.keys():
                                print '\tWARNING: Could not find variable {0} in nm output for file {1}'.format(variable, filename)

                        elif line[23] == 'F' and symbol_name in function_list:
                            print '\t{0}'.format(symbol_name)
                            sym_globalize_file.write('{0}\n'.format(symbol_name))

                sym_convert_file.close()
                sym_globalize_file.close()

                cmd = 'objcopy ' + filename + ' --redefine-syms=sym_convert'
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()
             
                cmd = 'objcopy ' + filename + ' --globalize-symbols=sym_globalize'
                p = subprocess.Popen(shlex.split(cmd),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                output,error = p.communicate()


    filename = tor_dir + 'or/main.o'
    variable = 'second_elapsed_callback'
    objcopy = 'objcopy ' + filename + ' --globalize-symbol ' + variable
    p = subprocess.Popen(shlex.split(objcopy),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    output,error = p.communicate()



    reg_file.write('\tsnri_register_globals({0},\n'.format(len(vars_global.keys()) + 2))
    reg_file.write('\t\tsizeof(scallion_t), scallion_global_data,\n')
    reg_file.write('\t\tsizeof(scallion_tp), scallion')
    for var in vars_global:
        reg_file.write(',\n\t\t0x{0}, &{1}'.format(vars_global[var], var))
        if var not in vars_to_skip: #and var not in specified_vars.keys():
            tor_externs.write('extern uint8_t {0}[{1}];\n'.format(var, int(vars_global[var], 16)))

    reg_file.write('\n')
    reg_file.write('\n\t);\n')
    reg_file.write('}\n')
    reg_file.close()

    tor_externs.close()


def create_scallion_registration(reg_file):
    reg_file.write('/**\n')
    reg_file.write(' * Scallion - plug-in for The Shadow Simulator\n')
    reg_file.write(' *\n')
    reg_file.write(' * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>\n')
    reg_file.write(' *\n')
    reg_file.write(' * This file is part of Scallion.\n')
    reg_file.write(' *\n')
    reg_file.write(' * Scallion is free software: you can redistribute it and/or modify\n')
    reg_file.write(' * it under the terms of the GNU General Public License as published by\n')
    reg_file.write(' * the Free Software Foundation, either version 3 of the License, or\n')
    reg_file.write(' * (at your option) any later version.\n')
    reg_file.write(' *\n')
    reg_file.write(' * Scallion is distributed in the hope that it will be useful,\n')
    reg_file.write(' * but WITHOUT ANY WARRANTY; without even the implied warranty of\n')
    reg_file.write(' * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n')
    reg_file.write(' * GNU General Public License for more details.\n')
    reg_file.write(' *\n')
    reg_file.write(' * You should have received a copy of the GNU General Public License\n')
    reg_file.write(' * along with Scallion.  If not, see <http://www.gnu.org/licenses/>.\n')
    reg_file.write(' */\n')
    reg_file.write('\n')
    reg_file.write('const char tor_git_revision[] = "";\n')
    reg_file.write('\n')
    reg_file.write('#include "scallion.h"\n')
    reg_file.write('\n')
    reg_file.write('#include "tor_includes.h"\n')
    reg_file.write('#include "tor_externs.h"\n')
    reg_file.write('\n')
    reg_file.write('void scallion_register_globals(scallion_t* scallion_global_data, scallion_tp* scallion) {\n')


if __name__ == '__main__':
    main()
