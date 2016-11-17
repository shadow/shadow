/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef int (*run_test_func)(void);

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## preload test starting ##########\n");


    if(argc != 2) {
        fprintf(stdout, "incorrect arg count '%i'\n", argc);
        fprintf(stdout, "########## preload test failed\n");
        return EXIT_FAILURE;
    }

    char* plugin_path = argv[1];

    fprintf(stdout, "dynamically loading test from '%s'\n", plugin_path);

    /* clear error string */
    dlerror();

    /* open the plugin that contains our test function */
    void* plugin_handle = dlmopen(LM_ID_NEWLM, plugin_path, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
    //void* plugin_handle = dlopen(plugin_path, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);

    if(!plugin_handle) {
        fprintf(stdout, "dlmopen() for path '%s' returned NULL, dlerror is '%s'\n", plugin_path, dlerror());
        fprintf(stdout, "########## preload test failed\n");
        return EXIT_FAILURE;
    }

    /* clear dlerror */
    dlerror();

    /* get our test function symbol so we can call it */
    run_test_func run_test = dlsym(plugin_handle, "run_test");

    if(!run_test) {
        fprintf(stdout, "dlsym() for symbol 'run_test' returned NULL, dlerror is '%s'\n", dlerror());
        fprintf(stdout, "########## preload test failed\n");
        return EXIT_FAILURE;
    }

    if(run_test() != 0) {
        fprintf(stdout, "test case returned failure\n");
        fprintf(stdout, "########## preload test failed\n");
        return EXIT_FAILURE;
    } else {
        fprintf(stdout, "########## preload test passed! ##########\n");
        return EXIT_SUCCESS;
    }
}
