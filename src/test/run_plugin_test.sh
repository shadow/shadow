#!/bin/bash

gcc `pkg-config --cflags --libs glib-2.0` -I/home/rob/.shadow/include -shared -Wl,-soname,testplugin.so -fPIC -o testplugin.so shd-test-plugin.c
cp testplugin.so /tmp/testplugin1.so
cp testplugin.so /tmp/testplugin2.so
cp testplugin.so /tmp/testplugin3.so
cp testplugin.so /tmp/testplugin4.so
gcc `pkg-config --cflags --libs glib-2.0 gthread-2.0 gmodule-2.0` -ldl shd-test-module.c -o shd-test-module
./shd-test-module
rm /tmp/testplugin*
