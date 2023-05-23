/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/process.h"

#include "main/bindings/c/bindings.h"

bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error) {
    GError* gError = NULL;

    bool rv = !!g_shell_parse_argv(commandLine, argc, argv, &gError);
    if (!rv && gError != NULL && gError->message != NULL && error != NULL) {
        *error = strdup(gError->message);
    }

    if (gError != NULL) {
        g_error_free(gError);
    }
    return rv;
}

void process_parseArgStrFree(char** argv, char* error) {
    if (argv != NULL) {
        g_strfreev(argv);
    }
    if (error != NULL) {
        g_free(error);
    }
}