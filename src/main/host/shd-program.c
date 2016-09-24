/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef __USE_GNU
#define __USE_GNU 1
#endif
#include <dlfcn.h>
#include <unistd.h>

#include "shadow.h"

/**
 * We call this function to run the plugin executable. A symbol with this name
 * must exist or the dlsym lookup will fail.
 */
#define PLUGIN_MAIN_SYMBOL "main"

/* Global symbols that plugins *may* define to hook changes in execution control */
#define PLUGIN_POSTLOAD_SYMBOL "__shadow_plugin_load__"
#define PLUGIN_PREUNLOAD_SYMBOL "__shadow_plugin_unload__"
#define PLUGIN_PREENTER_SYMBOL "__shadow_plugin_enter__"
#define PLUGIN_POSTEXIT_SYMBOL "__shadow_plugin_exit__"

typedef gint (*PluginMainFunc)(int argc, char* argv[]);
typedef void (*PluginHookFunc)(void* uniqueid);

struct _Program {
    GString* name;
    GString* path;
    void* handle;

    /* every plug-in needs a main function, which we call to start the virtual process */
    PluginMainFunc main;

    /* these functions allow us to notify the plugin code when we are passing control,
     * they are non-Null only if the plug-in optionally defines the symbols above */
    PluginHookFunc postLibraryLoad;
    PluginHookFunc preLibraryUnload;
    PluginHookFunc preProcessEnter;
    PluginHookFunc postProcessExit;

    /*
     * TRUE from when we've called into plug-in code until the call completes.
     * Note that the plug-in may get back into shadow code during execution, by
     * calling one of the shadowlib functions or calling a function that we
     * intercept. isShadowContext distinguishes this.
     */
    gboolean isExecuting;

    MAGIC_DECLARE;
};

static void program_callPostLibraryLoadHookFunc(Program* prog) {
    MAGIC_ASSERT(prog);
    if(prog->postLibraryLoad != NULL) {
        prog->postLibraryLoad(prog->handle);
    }
}

static void program_callPreLibraryUnloadHookFunc(Program* prog) {
    MAGIC_ASSERT(prog);
    if(prog->preLibraryUnload != NULL) {
        prog->preLibraryUnload(prog->handle);
    }
}

gint program_callMainFunc(Program* prog, gchar** argv, gint argc) {
    MAGIC_ASSERT(prog);
    utility_assert(prog->isExecuting);
    utility_assert(prog->main);
    return prog->main(argc, argv);
}

void program_callPreProcessEnterHookFunc(Program* prog) {
    MAGIC_ASSERT(prog);
    utility_assert(prog->isExecuting);
    if(prog->preProcessEnter != NULL) {
        prog->preProcessEnter(prog->handle);
    }
}

void program_callPostProcessExitHookFunc(Program* prog) {
    MAGIC_ASSERT(prog);
    utility_assert(prog->isExecuting);
    if(prog->postProcessExit != NULL) {
        prog->postProcessExit(prog->handle);
    }
}

void program_unload(Program* prog) {
    MAGIC_ASSERT(prog);

    if(prog->handle) {
        program_callPreLibraryUnloadHookFunc(prog);

        /* clear dlerror status string */
        dlerror();

        gboolean success = dlclose(prog->handle);
        if(dlclose(prog->handle) != 0) {
            const gchar* errorMessage = dlerror();
            warning("dlclose() failed: %s", errorMessage);
            warning("failed closing plugin '%s'", prog->path->str);
        }
    }

    prog->handle = NULL;
}

void program_load(Program* prog) {
    MAGIC_ASSERT(prog);

    if(prog->handle) {
        program_unload(prog);
    }

    /*
     * get the plugin handle from the library at filename.
     *
     * @warning only global dlopens are searchable with dlsym
     * we cant use G_MODULE_BIND_LOCAL if we want to be able to lookup
     * functions using dlsym in the plugin itself. if G_MODULE_BIND_LOCAL
     * functionality is desired, then we must require plugins to separate their
     * intercepted functions to a SHARED library, and link the plugin to that.
     *
     * We need a new namespace to keep state for each plugin separate.
     * From the manpage:
     *
     * ```
     * LM_ID_BASE
     * Load the shared object in the initial namespace (i.e., the application's namespace).
     *
     * LM_ID_NEWLM
     * Create  a new namespace and load the shared object in that namespace.  The object
     * must have been correctly linked to reference all of the other shared objects that
     * it requires, since the new namespace is initially empty.
     * ```
     */

    /* clear dlerror status string */
    dlerror();

    prog->handle = dlmopen(LM_ID_NEWLM, prog->path->str, RTLD_LAZY|RTLD_LOCAL|RTLD_DEEPBIND);

    if(prog->handle) {
        message("successfully loaded private plug-in '%s' at %p", prog->path->str, prog);
    } else {
        const gchar* errorMessage = dlerror();
        critical("dlmopen() failed: %s", errorMessage);
        error("unable to load private plug-in '%s'", prog->path->str);
    }

    /* clear dlerror status string */
    dlerror();

    /* make sure it has the required init function */
    gpointer function = NULL;

    function = dlsym(prog->handle, PLUGIN_MAIN_SYMBOL);
    if(function) {
        prog->main = function;
        message("found '%s' at %p", PLUGIN_MAIN_SYMBOL, function);
    } else {
        const gchar* errorMessage = dlerror();
        critical("dlsym() failed: %s", errorMessage);
        error("unable to find the required function symbol '%s' in plug-in '%s'",
                PLUGIN_MAIN_SYMBOL, prog->path->str);
    }

    /* clear dlerror status string */
    dlerror();

    function = NULL;
    function = dlsym(prog->handle, PLUGIN_POSTLOAD_SYMBOL);
    if(function) {
        prog->postLibraryLoad = function;
        message("found '%s' at %p", PLUGIN_POSTLOAD_SYMBOL, function);
    }

    function = NULL;
    function = dlsym(prog->handle, PLUGIN_PREUNLOAD_SYMBOL);
    if(function) {
        prog->preLibraryUnload = function;
        message("found '%s' at %p", PLUGIN_PREUNLOAD_SYMBOL, function);
    }

    function = NULL;
    function = dlsym(prog->handle, PLUGIN_PREENTER_SYMBOL);
    if(function) {
        prog->preProcessEnter = function;
        message("found '%s' at %p", PLUGIN_PREENTER_SYMBOL, function);
    }

    function = NULL;
    function = dlsym(prog->handle, PLUGIN_POSTEXIT_SYMBOL);
    if(function) {
        prog->postProcessExit = function;
        message("found '%s' at %p", PLUGIN_POSTEXIT_SYMBOL, function);
    }

    program_callPostLibraryLoadHookFunc(prog);
}

Program* program_new(const gchar* name, const gchar* path) {
    utility_assert(path);
    utility_assert(name);

    Program* prog = g_new0(Program, 1);
    MAGIC_INIT(prog);

    prog->name = g_string_new(name);
    prog->path = g_string_new(path);

    return prog;
}

void program_free(Program* prog) {
    MAGIC_ASSERT(prog);

    program_unload(prog);

    if(prog->path) {
        g_string_free(prog->path, TRUE);
    }
    if(prog->name) {
        g_string_free(prog->name, TRUE);
    }

    MAGIC_CLEAR(prog);
    g_free(prog);
}

void program_setExecuting(Program* prog, gboolean isExecuting) {
    MAGIC_ASSERT(prog);
    prog->isExecuting = isExecuting;
}

const gchar* program_getName(Program* prog) {
    MAGIC_ASSERT(prog);
    return prog->name->str;
}

const gchar* program_getPath(Program* prog) {
    MAGIC_ASSERT(prog);
    return prog->path->str;
}
