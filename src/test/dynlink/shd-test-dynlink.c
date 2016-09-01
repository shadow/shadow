/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/*
 * ./run_plugin_test.sh
 *
 * -- or --
 *
 * (see commands in shd-test-plugin first)
 * gcc `pkg-config --cflags --libs glib-2.0 gmodule-2.0` shd-test-module.c -o shd-test-module
 * ./shd-test-module
 */
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <link.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

//#include <glib.h>

#define RTLD_NEXT ((void *) -1l)

#define NUM_LOADS 8
#define PLUGIN_PATH "shadow-test-plugin-dynlink"
#define PLUGIN_MAIN_SYMBOL "main"
typedef int (*MainFunc)(int argc, char* argv[]);

//void load(gchar* path, gboolean useGlib) {
//    GModule* handle;
//
//    if(useGlib) {
//        handle = g_module_open(path, G_MODULE_BIND_LAZY|G_MODULE_BIND_LOCAL);
//    } else {
//        // note: RTLD_DEEPBIND -> prefer local symbols, but still have global access
//        handle = dlopen(path, RTLD_LAZY|RTLD_LOCAL);
//    }
//
//    if(handle) {
//        g_message("successfully loaded private plug-in '%s'", path);
//    } else {
//        g_error("unable to load private plug-in '%s'", path);
//    }
//
//    /* make sure it has the required init function */
//    InitFunc func;
//    gboolean success;
//
//    if(useGlib) {
//        success = g_module_symbol(handle, PLUGININITSYMBOL, (gpointer)&func);
//    } else {
//        /* must use handle to search when using G_MODULE_BIND_LOCAL
//         * using RTLD_NEXT when searching with G_MODULE_BIND_LOCAL will not work!
//         */
//        func = dlsym(handle, PLUGININITSYMBOL);
//        success = func ? TRUE : FALSE;
//    }
//
//    if(success) {
//        g_message("succesfully found function '%s' in plugin '%s'", PLUGININITSYMBOL, path);
//    } else {
//        g_error("unable to find the required function symbol '%s' in plug-in '%s'",
//                PLUGININITSYMBOL, path);
//    }
//
//    func();
//
////  Need to keep the modules open for the test
////  if(useGlib) {
////      g_module_close(handle);
////  } else {
////      dlclose(handle);
////  }
//}
//
///*
// * Without G_MODULE_BIND_LOCAL, the result is:
//    1 after increment
//    2 after increment
//    3 after increment
//    4 after increment
//
// * else the result is:
//    1 after increment
//    1 after increment
//    1 after increment
//    1 after increment
//
// * So, we need G_MODULE_BIND_LOCAL to keep variables private to the plugin.
// */
//
//void threadload(gchar* path, gpointer user_data) {
//    load(path, TRUE);
//}
//
//gchar* p1 = "/tmp/testplugin1.so";
//gchar* p2 = "/tmp/testplugin2.so";
//gchar* p3 = "/tmp/testplugin3.so";
//gchar* p4 = "/tmp/testplugin4.so";
//int main(void) {
//    load(p1, TRUE);
//    load(p2, TRUE);
//    load(p3, FALSE);
//    load(p4, FALSE);
//
//    /* so far it seems to work with or without threads */
//    g_thread_init(NULL);
//    GError *error = NULL;
//    GThreadPool* pool = g_thread_pool_new((GFunc)threadload, NULL, 2, TRUE, &error);
//
//    g_thread_pool_push(pool, p1, &error);
//    g_thread_pool_push(pool, p2, &error);
//    g_thread_pool_push(pool, p3, &error);
//    g_thread_pool_push(pool, p4, &error);
//
//    g_thread_pool_free(pool, FALSE, TRUE);
//    return EXIT_SUCCESS;
//}

void* _test_load_dlopen() {
    /* RTLD_LOCAL
     * Symbols defined in this library are not made available to resolve
     * references in subsequently loaded libraries
     *
     * RTLD_DEEPBIND
     * a self-contained library will use its own symbols in preference to global
     * symbols with the same name contained in libraries that have already been loaded
     */
    return dlopen(PLUGIN_PATH, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
}

void* _test_load_dlmopen() {
    // LM_ID_NEWLM, LM_ID_BASE
    return dlmopen(LM_ID_NEWLM, PLUGIN_PATH, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
}

int _test_linker_loader_single(int use_dlmopen) {
    void* handles[NUM_LOADS];
    MainFunc funcs[NUM_LOADS];

    for(int i = 0; i < NUM_LOADS; i++) {
        /* clear dlerror */
        dlerror();

        if(use_dlmopen) {
            handles[i] = _test_load_dlmopen();
        } else {
            handles[i] = _test_load_dlopen();
        }

        if(!handles[i]) {
            fprintf(stdout, "%s() for path '%s' returned NULL, dlerror is '%s'\n",
                    use_dlmopen ? "dlmopen" : "dlopen", PLUGIN_PATH, dlerror());
            return EXIT_FAILURE;
        }

        /* clear dlerror */
        dlerror();

        Lmid_t lmid;
        int result = dlinfo(handles[i], RTLD_DI_LMID, &lmid);

        if(result == 0) {
            fprintf(stdout, "found id %i for handle %p\n", (int)lmid, handles[i]);
        } else {
            fprintf(stdout, "error in dlinfo() for handle %p, dlerror is '%s'\n",
                    handles[i], dlerror());
        }

        /* clear dlerror */
        dlerror();

        funcs[i] = dlsym(handles[i], PLUGIN_MAIN_SYMBOL);
        if(!funcs[i]) {
            fprintf(stdout, "dlsym() for symbol '%s' returned NULL, dlerror is '%s'\n",
                    PLUGIN_MAIN_SYMBOL, dlerror());
            return EXIT_FAILURE;
        }
    }

    /* check /proc/<pid>/maps - the libs should refer to the same physical memory */
//    raise(SIGTSTP);

    int total_count = 0;
    for(int i = 0; i < NUM_LOADS; i++) {
        total_count += funcs[i](0, NULL);
    }
    int expected_count = 2*NUM_LOADS;

    fprintf(stdout, "total count is %i, expected count is %i\n", total_count, expected_count);

    /* check /proc/<pid>/maps - now we should have a copy-on-write for the incremented variables */
//    raise(SIGTSTP);

    int num_failures = 0;
    for(int i = 0; i < NUM_LOADS; i++) {
        /* clear dlerror */
        dlerror();

        if(dlclose(handles[i]) != 0) {
            num_failures++;
            fprintf(stdout, "dlclose() error for handle '%p', dlerror is '%s'\n",
                    handles[i], dlerror());
        }
    }

    if(total_count == expected_count && num_failures == 0) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## dynlink test starting ##########\n");

    if(_test_linker_loader_single(1) != 0) {
        fprintf(stdout, "########## _test_linker_loader_single() with dlmopen() failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## dynlink test passed! ##########\n");
    return EXIT_SUCCESS;
}
