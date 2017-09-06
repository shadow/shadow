
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_LOADS 500
#define PLUGIN_PATH "libplugin.so"
#define PLUGIN_SYM "main"

// new info type we've added
#define RTLD_DI_STATIC_TLS_SIZE 127

static void _print_tls_size(void* handle) {
    /* print the size of the buffer allocated for the TLS block */
    unsigned long tls_size;
    int result = dlinfo(handle, RTLD_DI_STATIC_TLS_SIZE, &tls_size);
    if (result == 0) {
        fprintf(stdout, "size of library static TLS after %d loads: %ld\n", NUM_LOADS, tls_size);
    } else {
        fprintf(stdout, "error in dlinfo() getting RTLD_DI_STATIC_TLS_SIZE for handle %p, dlerror is '%s'\n", handle, dlerror());
    }
}

int run(void) {
    void* handles[NUM_LOADS];

    for(int i = 0; i < NUM_LOADS; i++) {
        /* clear dlerror */
        dlerror();

        /* load the plugin */
        handles[i] = dlmopen(LM_ID_NEWLM, PLUGIN_PATH, RTLD_LAZY|RTLD_LOCAL);

        /* check for plugin load error */
        if(!handles[i]) {
            fprintf(stdout, "dlmopen() for path '%s' returned NULL, dlerror is '%s'\n",
                    PLUGIN_PATH, dlerror());
            return EXIT_FAILURE;
        }

        /* clear dlerror */
        dlerror();

        /* check the id of the new handle */
        Lmid_t lmid = 0;
        int result = dlinfo(handles[i], RTLD_DI_LMID, &lmid);

        if(result == 0) {
            fprintf(stdout, "found id %lu for handle %p\n", (long)lmid, handles[i]);
        } else {
            fprintf(stdout, "error in dlinfo() for handle %p, dlerror is '%s'\n",
                    handles[i], dlerror());
        }

        /* clear dlerror */
        dlerror();

        /* lookup a function symbol in the plugin we just loaded */
        void* func = dlsym(handles[i], PLUGIN_SYM);
        if(!func) {
            fprintf(stdout, "dlsym() for symbol '%s' returned NULL, dlerror is '%s'\n",
                    PLUGIN_SYM, dlerror());
            return EXIT_FAILURE;
        }
    }

    _print_tls_size(handles[NUM_LOADS-1]);

    // TODO close the dlmopened handles
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    int ret;
    if (!getenv("LD_STATIC_TLS_EXTRA")) {
        /* in this path, we calculate the static TLS size we would need */
        dlerror();
        unsigned long tls_size_start;
        /* we need a handle for dlinfo to work, even though we're not using it */
        void* handle = dlmopen(LM_ID_NEWLM, PLUGIN_PATH,
                RTLD_LAZY | RTLD_LOCAL);
        int result = dlinfo(handle, RTLD_DI_STATIC_TLS_SIZE, &tls_size_start);
        if (result != 0) {
            fprintf(stdout, "error in dlinfo() for handle %p, dlerror is '%s'\n",
                    handle, dlerror());
        }
        dlerror();
        handle = dlmopen(LM_ID_NEWLM, PLUGIN_PATH, RTLD_LAZY | RTLD_LOCAL);
        unsigned long tls_size_end;
        result = dlinfo(handle, RTLD_DI_STATIC_TLS_SIZE, &tls_size_end);
        if (result != 0) {
            fprintf(stdout,  "error in dlinfo() for handle %p, dlerror is '%s'\n",
                    handle, dlerror());
        }
        char call[100];
        unsigned long tls_size_to_allocate = (tls_size_end - tls_size_start)
                * NUM_LOADS;
        /* this strips environment variables, but there are ways to fix that */
        sprintf(call, "env LD_STATIC_TLS_EXTRA=%lu ./test",
                tls_size_to_allocate);
        system(call);
        ret = EXIT_SUCCESS;
    } else {
        /* the correct sized buffer has been allocated, run things as normal */
        ret = run();
    }
    return ret;
}
