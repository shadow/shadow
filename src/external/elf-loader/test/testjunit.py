#! /usr/bin/env python
## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-
import sys
import os
import subprocess
import junit_xml_output

test_cases = []

for arg in sys.argv[1:]:
    if '12' in arg:
        val = subprocess.Popen(['rm' ,'-f', 'libl.so'])
        val.wait ()
    
    cmd = ['./'+arg+'-ldso']
    test_env = os.environ.copy()
    test_env['LD_LIBRARY_PATH'] = '.:../'
    val = subprocess.Popen(cmd, 
                           stdout = subprocess.PIPE,
                           stderr = subprocess.PIPE, 
                           env = test_env)
    (stdout, stderr) = val.communicate()
    f = open ('output/' + arg, 'w')
    f.write (stdout)
    f.close ()
    if val.returncode != 0:
        test_cases.append(junit_xml_output.TestCase(arg, '(crash)\n '+ stderr,
                                                    "failure"))
        print 'CRASH ' + arg + '  -- LD_LIBRARY_PATH=.:../ ./' + arg + '-ldso'
    else:
        cmd = ['diff', '-q', 
               'output/' + arg,
               'output/' + arg + '.ref']
        val = subprocess.Popen(cmd, 
                               stdout = subprocess.PIPE,
                               stderr = subprocess.PIPE)
        (stdout, stderr) = val.communicate()
        #print stdout
        #print stderr
        if val.returncode != 0:
            test_cases.append(junit_xml_output.TestCase(arg, stderr,
                                                        "failure"))
            print 'FAIL ' + arg + '  -- LD_LIBRARY_PATH=.:../ ./' + arg + '-ldso'
        else:
            test_cases.append(junit_xml_output.TestCase(arg, stdout,
                                                        "success"))
            print 'PASS ' + arg

junit_xml = junit_xml_output.JunitXml("elf-loader-tests", test_cases)
f = open ('elf-loader-tests.xml', 'w')
f.write (junit_xml.dump())
f.close ()
