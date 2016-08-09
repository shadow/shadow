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
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

//#include <glib.h>

#define RTLD_NEXT ((void *) -1l)

#define NUM_LOADS 20
#define NUM_HARDLINKS 100
#define PLUGIN_PATH "libshadow-test-dynlink-plugin.so"
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

void* _test_load_dlopen(const char* plugin_path) {
    /* RTLD_LOCAL
     * Symbols defined in this library are not made available to resolve
     * references in subsequently loaded libraries
     *
     * RTLD_DEEPBIND
     * a self-contained library will use its own symbols in preference to global
     * symbols with the same name contained in libraries that have already been loaded
     */
    return dlopen(plugin_path, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
}

void* _test_load_dlmopen(const char* plugin_path) {
    // LM_ID_NEWLM, LM_ID_BASE
    return dlmopen(LM_ID_NEWLM, plugin_path, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);
}

int _test_linker_loader_single(int use_dlmopen) {
    void* handles[NUM_LOADS];
    MainFunc funcs[NUM_LOADS];

    for(int i = 0; i < NUM_LOADS; i++) {
        /* clear dlerror */
        dlerror();

        if(use_dlmopen) {
            handles[i] = _test_load_dlmopen(PLUGIN_PATH);
        } else {
            handles[i] = _test_load_dlopen(PLUGIN_PATH);
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

    if(num_failures > 0 || (use_dlmopen && (total_count != expected_count))) {
        return EXIT_FAILURE;
    } else {
        return EXIT_SUCCESS;
    }
}

static char* _get_temp_hard_link(const char* path) {
    char temp_path[256];
    memset(temp_path, 0, 256);

    // XXX not supposed to use tmpnam to get pathnames, since the path could
    // no longer be unique and empty by the time we use it
    char* p = tmpnam_r(temp_path);
    if(!p) {
        return NULL;
    }

    fprintf(stdout, "tmpnam returned path %s\n", p);

    if(link(path, p) == 0) {
        return strndup(p, (size_t)255);
    } else {
        return NULL;
    }
}

#include <glib/gstdio.h>
gboolean _test_copy(const gchar* source, const gchar* destination) {
    gchar* content;
    gsize length;
    GError *error = NULL;

    /* get the xml file */
    fprintf(stdout, "attempting to get contents of file '%s'\n", source);
    gboolean success = g_file_get_contents(source, &content, &length, &error);
    fprintf(stdout, "finished getting contents of file '%s'\n", source);

    /* check for success */
    if (!success) {
        fprintf(stdout, "error in g_file_get_contents: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }

    success = g_file_set_contents(destination, content, (gssize)length, &error);

    /* check for success */
    if (!success) {
        fprintf(stdout, "error in g_file_set_contents: %s\n", error->message);
        g_error_free(error);
        return FALSE;
    }

    g_free(content);
    return TRUE;
}
static char* _get_temp_file_copy(const char* path) {
    char template_path[256];
    memset(template_path, 0, 256);
    snprintf(template_path, (size_t)255, "XXXXXX-%s", path);

    /* try to open a templated file, checking for errors */
    gchar* temporaryFilename = NULL;
    GError* error = NULL;

    gint openedFile = g_file_open_tmp(template_path, &temporaryFilename, &error);
    if(openedFile < 0) {
        fprintf(stdout, "unable to open temporary file for cdata topology: %s\n", error->message);
        return NULL;
    }

    /* cleanup */
    close(openedFile);
    g_unlink(temporaryFilename);

    gboolean success = _test_copy(path, temporaryFilename);
    if(!success) {
        return NULL;
    }

    return temporaryFilename;
}

static int _test_linker_loader_newpaths(int do_link, int use_dlmopen) {
    void* handles[NUM_HARDLINKS];
    char* paths[NUM_HARDLINKS];

    for(int i = 0; i < NUM_HARDLINKS; i++) {
        /* clear dlerror */
        dlerror();

        if(do_link) {
            paths[i] = _get_temp_hard_link(PLUGIN_PATH);
        } else {
            paths[i] = _get_temp_file_copy(PLUGIN_PATH);
        }
        if(!paths[i]) {
            fprintf(stdout, "error creating hard link for path '%s': '%s'\n",
                                paths[i], strerror(errno));
            return EXIT_FAILURE;
        }

        if(use_dlmopen) {
            handles[i] = _test_load_dlmopen(paths[i]);
        } else {
            handles[i] = _test_load_dlopen(paths[i]);
        }
        if(!handles[i]) {
            fprintf(stdout, "dlmopen() for path '%s' returned NULL, dlerror is '%s'\n",
                    paths[i], dlerror());
            return EXIT_FAILURE;
        }

        fprintf(stdout, "got handle %p for path '%s'\n", handles[i], paths[i]);
    }

    int num_failures = 0;
    for(int i = 0; i < NUM_HARDLINKS; i++) {
        /* clear dlerror */
        dlerror();

        if(handles[i] && dlclose(handles[i]) != 0) {
            num_failures++;
            fprintf(stdout, "dlclose() error for path '%s' and handle '%p', dlerror is '%s'\n",
                    paths[i], handles[i], dlerror());
        }
    }

    if(num_failures == 0) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

int thread_run() {
    /**
     * Test result:
     * dlopen can load a file multiple times, but not it their own namespace
     * so this doesnt work for shadow virtual nodes
     */
//    fprintf(stdout, "########## dynlink testing dlopen ##########\n");
//
//    if(_test_linker_loader_single(0) != 0) {
//        fprintf(stdout, "########## _test_linker_loader_single() with dlopen() failed\n");
//        return -EXIT_FAILURE;
//    }


    /**
     * Test result:
     * dlmopen can load a plugin in their own namespace, but runs out of TLS
     * "slots" after opening 13 libs
     */
    fprintf(stdout, "########## dynlink testing dlmopen ##########\n");

    if(_test_linker_loader_single(1) != 0) {
        fprintf(stdout, "########## _test_linker_loader_single() with dlmopen() failed\n");
        return -EXIT_FAILURE;
    }


    /**
     * Test result:
     * file copies do work - dlopen gives new handles for plugin copies
     * but dlopen still doesnt give us new namespaces for libs linked to plugin
     */
//    fprintf(stdout, "########## dynlink testing dlopen with file copies ##########\n");
//
//    if(_test_linker_loader_newpaths(0, 0) != 0) {
//        fprintf(stdout, "########## _test_linker_loader_newpaths(copy) with dlopen() failed\n");
//        return -EXIT_FAILURE;
//    }


    /**
     * Test result:
     * dlmopen gives us new handles and new namespaces for file copies
     * but still only lets us open 13 times, regardless of the file paths
     */
    fprintf(stdout, "########## dynlink testing dlmopen with file copies ##########\n");

    if(_test_linker_loader_newpaths(0, 1) != 0) {
        fprintf(stdout, "########## _test_linker_loader_newpaths(copy) with dlmopen() failed\n");
        return -EXIT_FAILURE;
    }


    /**
     * Test result:
     * hardlinks dont work - dlopen gives the same handle for diff hard link paths
     */
//    fprintf(stdout, "########## dynlink testing dlopen with hardlinks ##########\n");
//
//    if(_test_linker_loader_newpaths(1, 0) != 0) {
//        fprintf(stdout, "########## _test_linker_loader_newpaths(link) with dlopen() failed\n");
//        return -EXIT_FAILURE;
//    }


    /**
     * Test result:
     * dlmopen gives us new handles and new namespaces for hard links
     * but still only lets us open 13 times, regardless of the file paths
     */
    fprintf(stdout, "########## dynlink testing dlmopen with hardlinks ##########\n");

    if(_test_linker_loader_newpaths(1, 1) != 0) {
        fprintf(stdout, "########## _test_linker_loader_newpaths(link) with dlmopen() failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## dynlink test passed! ##########\n");
    return EXIT_SUCCESS;
}

void* thread_main(void* arg) {
    thread_run();
    return NULL;
}

int start_thread() {
    pthread_t slave_tid;
    pthread_attr_t slave_attr;
    size_t stack_size;
    void *stack_addr;

    pthread_attr_init(&slave_attr);
    // Stack size is 128M bytes
    stack_size = 128 * 1024 * 1024;
    posix_memalign(&stack_addr, sysconf(_SC_PAGESIZE), stack_size);
    pthread_attr_setstack(&slave_attr, stack_addr, stack_size);

    pthread_create(&slave_tid, &slave_attr, thread_main, NULL);
    pthread_attr_destroy(&slave_attr);

    pthread_join(slave_tid, NULL);
    return 0;
}

int main(int argc, char* argv[]) {
    // run in a thread to see if we can increase the thread stack size
    // and if dlmopen would then succeed
    // test result: nope, thread and thread stack size doesnt matter
    start_thread();
    // test result: multiple threads cannot open any more libs than main thread
    start_thread();
    return 0;
}
