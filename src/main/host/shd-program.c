/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <unistd.h>
#include <glib/gstdio.h>

#include "shadow.h"

/**
 * We call this function to run the plugin executable. A symbol with this name
 * must exist or the dlsym lookup will fail.
 */
#define PLUGIN_MAIN_SYMBOL "main"

/**
 * Global symbols added after using LLVM to automatically extract variable state
 */
#define PLUGIN_GLOBALS_SYMBOL "__hoisted_globals"
#define PLUGIN_GLOBALS_SIZE_SYMBOL "__hoisted_globals_size"
#define PLUGIN_GLOBALS_POINTER_SYMBOL "__hoisted_globals_pointer"

/* Global symbols that plugins may define to hook changes in execution control */
#define PLUGIN_POSTLOAD_SYMBOL "__shadow_plugin_load__"
#define PLUGIN_PREUNLOAD_SYMBOL "__shadow_plugin_unload__"
#define PLUGIN_PREENTER_SYMBOL "__shadow_plugin_enter__"
#define PLUGIN_POSTEXIT_SYMBOL "__shadow_plugin_exit__"

typedef gint (*PluginMainFunc)(int argc, char* argv[]);
typedef void (*PluginHookFunc)(void* uniqueid);

struct _Program {
    GQuark id;

    GString* name;
    GString* path;
    GModule* handle;
    gboolean isTemporary;

    PluginMainFunc main;

    /* these functions allow us to notify the plugin code when we are passing control,
     * they are non-Null only if the plug-in optionally defines the symbols above */
    PluginHookFunc postLibraryLoad;
    PluginHookFunc preLibraryUnload;
    PluginHookFunc preProcessEnter;
    PluginHookFunc postProcessExit;

    gsize residentStateSize;
    gpointer residentStatePointer;
    gpointer residentState;
    ProgramState defaultState;

    /*
     * TRUE from when we've called into plug-in code until the call completes.
     * Note that the plug-in may get back into shadow code during execution, by
     * calling one of the shadowlib functions or calling a function that we
     * intercept. isShadowContext distinguishes this.
     */
    gboolean isExecuting;

    MAGIC_DECLARE;
};

void program_swapInState(Program* prog, ProgramState state) {
    MAGIC_ASSERT(prog);
    utility_assert(!prog->isExecuting);

    /* context switch from shadow to plug-in library
     *
     * TODO: we can be smarter here - save a pointer to the last plugin that
     * was loaded... if the physical memory locations still has our state,
     * there is no need to copy it in again. similarly for stopExecuting()
     */
    /* destination, source, size */
    g_memmove(prog->residentState, state, prog->residentStateSize);

    prog->isExecuting = TRUE;
}

void program_swapOutState(Program* prog, ProgramState state) {
    MAGIC_ASSERT(prog);
    utility_assert(prog->isExecuting);

    prog->isExecuting = FALSE;

    /* destination, source, size */
    g_memmove(state, prog->residentState, prog->residentStateSize);
}

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

static GString* _program_getTemporaryFilePath(gchar* originalPath) {
    /* get the basename of the real plug-in and create a temp file template */
    gchar* basename = g_path_get_basename(originalPath);
    GString* templateBuffer = g_string_new(basename);
    g_free(basename);

    templateBuffer = g_string_prepend(templateBuffer, "XXXXXX-");
    gchar* template = g_string_free(templateBuffer, FALSE);

    /* try to open the templated file, checking for errors */
    gchar* temporaryFilename = NULL;
    GError* error = NULL;
    gint openedFile = g_file_open_tmp((const gchar*) template, &temporaryFilename, &error);
    if(openedFile < 0) {
        error("unable to open temporary file for plug-in '%s': %s", originalPath, error->message);
    }

    /* now we ceanup and return the new filename */
    close(openedFile);
    g_free(template);

    GString* templatePath = g_string_new(temporaryFilename);
    g_free(temporaryFilename);
    return templatePath;
}

static gboolean _program_copyFile(gchar* fromPath, gchar* toPath) {
    gchar* contents = NULL;
    gsize length = 0;
    GError* error = NULL;

    /* get the original file */
    if(!g_file_get_contents(fromPath, &contents, &length, &error)) {
        error("unable to read '%s' for copying: %s", fromPath, error->message);
        return FALSE;
    }
    error = NULL;

    /* copy to the new file */
    if(!g_file_set_contents(toPath, contents, (gssize)length, &error)) {
        error("unable to write private copy of '%s' to '%s': %s",
                fromPath, toPath, error->message);
        return FALSE;
    }

    /* ok, our private copy was created, cleanup */
    g_free(contents);
    return TRUE;
}

Program* program_new(const gchar* name, const gchar* path) {
    utility_assert(path);

    Program* prog = g_new0(Program, 1);
    MAGIC_INIT(prog);

    prog->id = g_quark_from_string((const gchar*) name);;
    prog->name = g_string_new(name);
    prog->path = g_string_new(path);

    /*
     * now get the plugin handle from the library at filename.
     *
     * @warning only global dlopens are searchable with dlsym
     * we cant use G_MODULE_BIND_LOCAL if we want to be able to lookup
     * functions using dlsym in the plugin itself. if G_MODULE_BIND_LOCAL
     * functionality is desired, then we must require plugins to separate their
     * intercepted functions to a SHARED library, and link the plugin to that.
     *
     * @note this will call g_module_check_init() in the plug-in if it contains
     * that function.
     */
    prog->handle = g_module_open(prog->path->str, G_MODULE_BIND_LAZY|G_MODULE_BIND_LOCAL);
    if(prog->handle) {
        message("successfully loaded private plug-in '%s' at %p", prog->path->str, prog);
    } else {
        const gchar* errorMessage = g_module_error();
        critical("g_module_open() failed: %s", errorMessage);
        error("unable to load private plug-in '%s'", prog->path->str);
    }

    /* make sure it has the required init function */
    gpointer function = NULL;
    gboolean success = FALSE;

    success = g_module_symbol(prog->handle, PLUGIN_MAIN_SYMBOL, &function);
    if(success) {
        prog->main = function;
        message("found '%s' at %p", PLUGIN_MAIN_SYMBOL, function);
    } else {
        const gchar* errorMessage = g_module_error();
        critical("g_module_symbol() failed: %s", errorMessage);
        error("unable to find the required function symbol '%s' in plug-in '%s'",
                PLUGIN_MAIN_SYMBOL, path);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_GLOBALS_SYMBOL, &function);
    if(success) {
        prog->residentState = function;
        message("found '%s' at %p", PLUGIN_GLOBALS_SYMBOL, function);
    } else {
        const gchar* errorMessage = g_module_error();
        critical("g_module_symbol() failed: %s", errorMessage);
        error("unable to find the required merged globals struct symbol '%s' in plug-in '%s'",
                PLUGIN_GLOBALS_SYMBOL, path);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_GLOBALS_POINTER_SYMBOL, &function);
    if(success) {
        prog->residentStatePointer = function;
        message("found '%s' at %p", PLUGIN_GLOBALS_POINTER_SYMBOL, function);
    } else {
        const gchar* errorMessage = g_module_error();
        critical("g_module_symbol() failed: %s", errorMessage);
        error("unable to find the required merged globals struct symbol '%s' in plug-in '%s'",
                PLUGIN_GLOBALS_POINTER_SYMBOL, path);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_GLOBALS_SIZE_SYMBOL, &function);
    if(success) {
        utility_assert(function);
        gint s = *((gint*) function);
        prog->residentStateSize = (gsize) s;
        message("found '%s' of value '%i' at %p", PLUGIN_GLOBALS_SIZE_SYMBOL, s, function);
    } else {
        const gchar* errorMessage = g_module_error();
        critical("g_module_symbol() failed: %s", errorMessage);
        error("unable to find the required merged globals struct symbol '%s' in plug-in '%s'",
                PLUGIN_GLOBALS_SIZE_SYMBOL, path);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_POSTLOAD_SYMBOL, &function);
    if(success) {
        prog->postLibraryLoad = function;
        message("found '%s' at %p", PLUGIN_POSTLOAD_SYMBOL, function);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_PREUNLOAD_SYMBOL, &function);
    if(success) {
        prog->preLibraryUnload = function;
        message("found '%s' at %p", PLUGIN_PREUNLOAD_SYMBOL, function);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_PREENTER_SYMBOL, &function);
    if(success) {
        prog->preProcessEnter = function;
        message("found '%s' at %p", PLUGIN_PREENTER_SYMBOL, function);
    }

    function = NULL;
    success = g_module_symbol(prog->handle, PLUGIN_POSTEXIT_SYMBOL, &function);
    if(success) {
        prog->postProcessExit = function;
        message("found '%s' at %p", PLUGIN_POSTEXIT_SYMBOL, function);
    }

    program_callPostLibraryLoadHookFunc(prog);

    /* finally, store a copy of the defaults as they exist now */
    debug("copying resident plugin memory contents at %p-%p (%"G_GSIZE_FORMAT" bytes) as default start state",
            prog->residentState, prog->residentState+prog->residentStateSize, prog->residentStateSize);
    prog->defaultState = g_slice_copy(prog->residentStateSize, prog->residentState);
    debug("stored default state at %p", prog->defaultState);

    return prog;
}

void program_free(Program* prog) {
    MAGIC_ASSERT(prog);

    if(prog->handle) {
        program_callPreLibraryUnloadHookFunc(prog);
        gboolean success = g_module_close(prog->handle);
        if(!success) {
            const gchar* errorMessage = g_module_error();
            warning("g_module_close() failed: %s", errorMessage);
            warning("failed closing plugin '%s'", prog->path->str);
        }
    }

    /* TODO: this unlink should be removed when we no longer copy plugins
     * before loading them. see the other TODO above in this file.
     */
    if(prog->isTemporary) {
        g_unlink(prog->path->str);
    }
    if(prog->path) {
        g_string_free(prog->path, TRUE);
    }
    if(prog->name) {
        g_string_free(prog->name, TRUE);
    }

    if(prog->defaultState) {
        program_freeState(prog, prog->defaultState);
    }

    MAGIC_CLEAR(prog);
    g_free(prog);
}

Program* program_getTemporaryCopy(Program* prog) {
    utility_assert(prog);

    /* do not open the path directly, but rather copy to tmp directory first
     * to avoid multiple threads using the same memory space.
     * TODO: this should eventually be replaced when we have thread-local
     * storage working correctly in the LLVM module-pass code */
    GString* pathCopy = _program_getTemporaryFilePath(prog->path->str);

    /* now we need to copy the actual contents to our new file */
    if(!_program_copyFile(prog->path->str, pathCopy->str)) {
        g_string_free(pathCopy, TRUE);
        return NULL;
    }

    Program* progCopy = program_new(prog->name->str, pathCopy->str);
    progCopy->isTemporary = TRUE;

    g_string_free(pathCopy, TRUE);

    return progCopy;
}

ProgramState program_newDefaultState(Program* prog) {
    MAGIC_ASSERT(prog);
    return g_slice_copy(prog->residentStateSize, prog->defaultState);
}

void program_freeState(Program* prog, gpointer state) {
    MAGIC_ASSERT(prog);
    g_slice_free1(prog->residentStateSize, state);
}

GQuark* program_getID(Program* prog) {
    MAGIC_ASSERT(prog);
    return &(prog->id);
}

const gchar* program_getName(Program* prog) {
    MAGIC_ASSERT(prog);
    return prog->name->str;
}

const gchar* program_getPath(Program* prog) {
    MAGIC_ASSERT(prog);
    return prog->path->str;
}

void* program_getHandle(Program* prog) {
    MAGIC_ASSERT(prog);
    return prog->handle;
}
