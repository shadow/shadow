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

//#include <glib.h>

#define RTLD_NEXT ((void *) -1l)

// this is for the new dlinfo type we've added to elf-loader
#define RTLD_DI_STATIC_TLS_SIZE 127

#define NUM_LOADS 500
#define NUM_HARDLINKS 100
#define PLUGIN_PATH "libshadow-test-dynlink-plugin.so"

#define PLUGIN_MAIN_SYMBOL "main"
typedef int (*MainFunc)(int argc, char* argv[]);

int global_num_dlmopens = 0;
long unsigned int global_link_counter = 0;

static void _test_print_tls_size(void* handle) {
    /* print the size of the buffer allocated for the TLS block */
    unsigned long tls_size;
    int result = dlinfo(handle, RTLD_DI_STATIC_TLS_SIZE, &tls_size);
    if (result == 0) {
        fprintf(stdout, "size of library static TLS after %d loads: %ld\n", global_num_dlmopens, tls_size);
    } else {
        fprintf(stdout, "error in dlinfo() getting RTLD_DI_STATIC_TLS_SIZE for handle %p, dlerror is '%s'\n", handle, dlerror());
    }
}

void* _test_load_dlopen(const char* plugin_path) {
    /*
     * RTLD_LOCAL
     * Symbols defined in this library are not made available to resolve
     * references in subsequently loaded libraries
     */
    return dlopen(plugin_path, RTLD_LAZY|RTLD_LOCAL);
}

void* _test_load_dlmopen(const char* plugin_path) {
    /*
     * LM_ID_BASE
     * Load the shared object in the initial namespace (i.e., the application's namespace).
     *
     * LM_ID_NEWLM
     * Create  a new namespace and load the shared object in that namespace.  The object
     * must have been correctly linked to reference all of the other shared objects that
     * it requires, since the new namespace is initially empty.
     */
    global_num_dlmopens++;
    return dlmopen(LM_ID_NEWLM, plugin_path, RTLD_LAZY|RTLD_LOCAL);
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

        Lmid_t lmid = 0;
        int result = dlinfo(handles[i], RTLD_DI_LMID, &lmid);

        if(result == 0) {
            fprintf(stdout, "found id %lu for handle %p, num loads=%i\n", (long unsigned int)lmid, handles[i], global_num_dlmopens);
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

    _test_print_tls_size(handles[0]);

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

#include <glib.h>
#include <glib/gstdio.h>

static char* _get_temp_hard_link(const char* path) {
    GString* s = g_string_new(NULL);
    g_string_append_printf(s, "temp-%09lu-%s", ++global_link_counter, path);

    char* p = s->str;
    g_string_free(s, 0);

    fprintf(stdout, "created path for link at %s\n", p);

    if(link(path, p) == 0) {
        return p;
    } else {
        g_free(p);
        return NULL;
    }
}

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

    _test_print_tls_size(handles[0]);

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

int test_dynlink_dlopen() {
    /**
     * Test result:
     * dlopen can load a file multiple times, but not it their own namespace
     * so this doesnt work for shadow virtual nodes
     */
    fprintf(stdout, "########## dynlink testing dlopen ##########\n");

    if(_test_linker_loader_single(0) != 0) {
        fprintf(stdout, "########## _test_linker_loader_single() with dlopen() failed\n");
        return -EXIT_FAILURE;
    }

    /**
     * Test result:
     * file copies do work - dlopen gives new handles for plugin copies
     * but dlopen still doesnt give us new namespaces for libs linked to plugin
     */
    fprintf(stdout, "########## dynlink testing dlopen with file copies ##########\n");

    if(_test_linker_loader_newpaths(0, 0) != 0) {
        fprintf(stdout, "########## _test_linker_loader_newpaths(copy) with dlopen() failed\n");
        return -EXIT_FAILURE;
    }


    /**
     * Test result:
     * hardlinks dont work - dlopen gives the same handle for diff hard link paths
     */
    fprintf(stdout, "########## dynlink testing dlopen with hardlinks ##########\n");

    if(_test_linker_loader_newpaths(1, 0) != 0) {
        fprintf(stdout, "########## _test_linker_loader_newpaths(link) with dlopen() failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## dynlink dlopen test passed! ##########\n");
    return EXIT_SUCCESS;
}

int test_dynlink_dlmopen() {
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

    return EXIT_SUCCESS;
}

int test_dynlink_dlmopen_extended() {
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
     * dlmopen gives us new handles and new namespaces for hard links
     * but still only lets us open 13 times, regardless of the file paths
     */
    fprintf(stdout, "########## dynlink testing dlmopen with hardlinks ##########\n");

    if(_test_linker_loader_newpaths(1, 1) != 0) {
        fprintf(stdout, "########## _test_linker_loader_newpaths(link) with dlmopen() failed\n");
        return -EXIT_FAILURE;
    }

    fprintf(stdout, "########## dynlink dlmopen test passed! ##########\n");
    return EXIT_SUCCESS;
}

int test_dynlink_run() {
    if(test_dynlink_dlmopen() != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

//    if(test_dynlink_dlmopen_extended() != EXIT_SUCCESS) {
//        return EXIT_FAILURE;
//    }

    return EXIT_SUCCESS;
}

//#include <pthread.h>
//void* thread_main(void* arg) {
//    test_run();
//    return NULL;
//}
//
//int start_thread() {
//    pthread_t slave_tid;
//    pthread_attr_t slave_attr;
//    size_t stack_size;
//    void *stack_addr;
//
//    pthread_attr_init(&slave_attr);
//    // Stack size is 128M bytes
//    stack_size = 128 * 1024 * 1024;
//    posix_memalign(&stack_addr, sysconf(_SC_PAGESIZE), stack_size);
//    pthread_attr_setstack(&slave_attr, stack_addr, stack_size);
//
//    pthread_create(&slave_tid, &slave_attr, test_dynlink_run, NULL);
//    pthread_attr_destroy(&slave_attr);
//
//    pthread_join(slave_tid, NULL);
//    return 0;
//}
//
//int main(int argc, char* argv[]) {
//     run in a thread to see if we can increase the thread stack size
//     and if dlmopen would then succeed
//     test result: nope, thread and thread stack size doesnt matter
//     test result: multiple threads cannot open any more libs than main thread
//    start_thread();
//    start_thread();
//    return test_dynlink_run();
//}

static unsigned long _test_compute_static_tls_size() {
    unsigned long tls_size_start = 0;
    unsigned long tls_size_end = 0;

    /* clear error */
    dlerror();

    /* we need a handle for dlinfo to work, even though we're not using it */
    void* handle = _test_load_dlmopen(PLUGIN_PATH);
    int result = dlinfo(handle, RTLD_DI_STATIC_TLS_SIZE, &tls_size_start);

    if (result != 0) {
        fprintf(stdout, "error in dlinfo() for handle %p, dlerror is '%s'\n", handle, dlerror());
        return 0;
    }

    /* clear error */
    dlerror();

    handle = _test_load_dlmopen(PLUGIN_PATH);
    result = dlinfo(handle, RTLD_DI_STATIC_TLS_SIZE, &tls_size_end);

    if (result != 0) {
        fprintf(stdout, "error in dlinfo() for handle %p, dlerror is '%s'\n", handle, dlerror());
        return 0;
    }

    unsigned long single_load_size = (tls_size_end - tls_size_start);

    /* we have 3 dlmopen tests, one opens NUM_LOADS, the other two open NUM_HARDLINKS */
    unsigned long tls_size_to_allocate = single_load_size * (NUM_LOADS+2*NUM_HARDLINKS);

    /* make sure we dont return 0 when successful, and default to a lower limit of 1024 */
    if(tls_size_to_allocate < 1024) {
        tls_size_to_allocate = 1024;
    }
    return tls_size_to_allocate;
}

int main_shadow(int argc, char* argv[]) {
    return test_dynlink_run();
}

int main_no_shadow(int argc, char* argv[]) {
    int ret;
    if (!getenv("LD_STATIC_TLS_EXTRA")) {
        /* in this path, we calculate the static TLS size we would need */
        unsigned long tls_size_to_allocate = _test_compute_static_tls_size();

        char* call = NULL;

        /* this strips environment variables, but there are ways to fix that */
        asprintf(&call, "env LD_STATIC_TLS_EXTRA=%lu %s", tls_size_to_allocate, argv[0]);

        /* restart the process with the correct static tls size */
        ret = system(call);

        free(call);

        if(ret == 0) {
            ret = EXIT_SUCCESS;
        } else {
            ret = EXIT_FAILURE;
        }
    } else {
        /* the correct sized buffer has been allocated, run things as normal */
        ret = test_dynlink_run();
    }
    return ret;
}
