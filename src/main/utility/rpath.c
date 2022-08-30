/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <elf.h>
#include <errno.h>
#include <glib.h>
#include <link.h>

#include "main/utility/utility.h"

static gchar* _getRPath() {
    const ElfW(Dyn) *dyn = _DYNAMIC;
    const ElfW(Dyn) *rpath = NULL;
    const gchar *strtab = NULL;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        if (dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
            rpath = dyn;
        } else if (dyn->d_tag == DT_STRTAB) {
            strtab = (const gchar *) dyn->d_un.d_val;
        }
    }
    GString* rpathStrBuf = g_string_new(NULL );
    if (strtab != NULL && rpath != NULL ) {
        g_string_printf(rpathStrBuf, "%s", strtab + rpath->d_un.d_val);
    }
    return g_string_free(rpathStrBuf, FALSE);
}

static gboolean _isValidPathToPreloadLib(const gchar* path, const gchar* libname) {
    if(path) {
        gboolean isAbsolute = g_path_is_absolute(path);
        gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS);
        gboolean hasLibName = g_str_has_suffix(path, libname);

        if (isAbsolute && exists && hasLibName) {
            return TRUE;
        }
    }

    return FALSE;
}

static gchar* _getOrigin() {
    gchar* exePath = g_file_read_link("/proc/self/exe", NULL);
    utility_alwaysAssert(exePath);
    gchar* dirName = g_path_get_dirname(exePath);
    utility_alwaysAssert(dirName);
    g_free(exePath);
    return dirName;
}

// Replace the string "$ORIGIN" with the directory of the current-running executable.
// See "Rpath token expansion" in ls.do(8).
//
// This mechanism allows us to set an rpath in shadow relative to the shadow binary,
// which in turn makes the shadow installation directory relocatable.
static gchar* _substituteOrigin(const gchar* in) {
    gchar* origin = _getOrigin();
    GRegex* originRegex = g_regex_new("\\$ORIGIN\\b", 0, 0, NULL);
    utility_alwaysAssert(originRegex);
    gchar* out = g_regex_replace(originRegex, in, strlen(in), 0, origin, 0, NULL);
    utility_alwaysAssert(out);
    g_free(origin);
    g_regex_unref(originRegex);
    return out;
}

gchar* scanRpathForLib(const gchar* libname) {
    gchar* preloadArgValue = NULL;

    gchar* originalRpathStr = _getRPath();
    gchar* rpathStr = _substituteOrigin(originalRpathStr);
    g_free(originalRpathStr);

    if(rpathStr != NULL) {
        gchar** tokens = g_strsplit(rpathStr, ":", 0);

        for(gint i = 0; tokens[i] != NULL; i++) {
            GString* candidateBuffer = g_string_new(NULL);

            /* rpath specifies directories, so look inside */
            g_string_printf(candidateBuffer, "%s/%s", tokens[i], libname);
            gchar* candidate = g_string_free(candidateBuffer, FALSE);

            if (_isValidPathToPreloadLib(candidate, libname)) {
                preloadArgValue = candidate;
                break;
            } else {
                g_free(candidate);
            }
        }

        g_strfreev(tokens);
    }
    g_free(rpathStr);

    return preloadArgValue;
}
