/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/*
 * gcc -shared -Wl,-soname,testplugin.so -fPIC -o testplugin.so shd-test-plugin.c
 * cp testplugin.so /tmp/testplugin1.so
 * cp testplugin.so /tmp/testplugin2.so
 *
 * #updated as valid shadow plugin
 * gcc -shared -Wl,-soname,testplugin.so -fPIC -o testplugin.so shd-test-plugin.c `pkg-config --cflags --libs glib-2.0` -I/home/rob/.shadow/include
 */

extern int lib_increment();

int plugin_value = 0;

int main(int argc, char* argv[]) {
    return ++plugin_value + lib_increment();
}
