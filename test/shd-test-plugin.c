/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * gcc -shared -Wl,-soname,testplugin.so -fPIC -o testplugin.so shd-test-plugin.c
 * cp testplugin.so /tmp/testplugin1.so
 * cp testplugin.so /tmp/testplugin2.so
 *
 * #updated as valid shadow plugin
 * gcc -shared -Wl,-soname,testplugin.so -fPIC -o testplugin.so shd-test-plugin.c `pkg-config --cflags --libs glib-2.0` -I/home/rob/.shadow/include
 */

#include <stdio.h>
#include <glib.h>
#include <shd-library.h>

ShadowFunctionTable* table;
int test = 0;

void __init__() {
    test++;
    printf("%i after increment\n", test);
}

void _new(int argc, char* argv[]) {
    test++;
    table->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
                    "new node, %i total, %p", test, &test);
}

void _free() {

}

void _ready() {

}

/* function table for Shadow so it knows how to call us */
PluginFunctionTable pluginFunctions = {
    &_new, &_free, &_ready,
};

void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
    g_assert(shadowlibFuncs);
    table = shadowlibFuncs;

    /* start out with cleared state */
    test = 0;
    table->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
                    "registered node, start at %i, %p", test, &test);

    /*
     * tell shadow which of our functions it can use to notify our plugin,
     * and allow it to track our state for each instance of this plugin
     *
     * we 'register' our function table, and 1 variable.
     */
    gboolean success = shadowlibFuncs->registerPlugin(&pluginFunctions, 1, sizeof(int), &test);

    /* we log through Shadow by using the log function it supplied to us */
    if(success) {
        shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
                "successfully registered echo plug-in state");
    } else {
        shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
                "error registering echo plug-in state");
    }
}
