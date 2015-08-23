/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include <execinfo.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gstdio.h>

guint utility_ipPortHash(in_addr_t ip, in_port_t port) {
    GString* buffer = g_string_new(NULL);
    g_string_printf(buffer, "%u:%u", ip, port);
    guint hash_value = g_str_hash(buffer->str);
    g_string_free(buffer, TRUE);
    return hash_value;
}

guint utility_int16Hash(gconstpointer value) {
    utility_assert(value);
    /* make sure upper bits are zero */
    gint key = 0;
    key = (gint) *((gint16*)value);
    return g_int_hash(&key);
}

gboolean utility_int16Equal(gconstpointer value1, gconstpointer value2) {
    utility_assert(value1 && value2);
    /* make sure upper bits are zero */
    gint key1 = 0, key2 = 0;
    key1 = (gint) *((gint16*)value1);
    key2 = (gint) *((gint16*)value2);
    return g_int_equal(&key1, &key2);
}

gint utility_doubleCompare(const gdouble* value1, const gdouble* value2, gpointer userData) {
    utility_assert(value1 && value2);
    /* return neg if first before second, pos if second before first, 0 if equal */
    return (*value1) == (*value2) ? 0 : (*value1) < (*value2) ? -1 : +1;
}

gint utility_simulationTimeCompare(const SimulationTime* value1, const SimulationTime* value2,
        gpointer userData) {
    utility_assert(value1 && value2);
    /* return neg if first before second, pos if second before first, 0 if equal */
    return (*value1) == (*value2) ? 0 : (*value1) < (*value2) ? -1 : +1;
}

gchar* utility_getHomePath(const gchar* path) {
    GString* sbuffer = g_string_new("");
    if(g_ascii_strncasecmp(path, "~", 1) == 0) {
        /* replace ~ with home directory */
        const gchar* home = g_get_home_dir();
        g_string_append_printf(sbuffer, "%s%s", home, path+1);
    } else {
        g_string_append_printf(sbuffer, "%s", path);
    }
    return g_string_free(sbuffer, FALSE);
}

guint utility_getRawCPUFrequency(const gchar* freqFilename) {
    /* get the raw speed of the experiment machine */
    guint rawFrequencyKHz = 0;
    gchar* contents = NULL;
    gsize length = 0;
    GError* error = NULL;
    if(freqFilename && g_file_get_contents(freqFilename, &contents, &length, &error)) {
        utility_assert(contents);
        rawFrequencyKHz = (guint)atoi(contents);
        g_free(contents);
    }
    if(error) {
        g_error_free(error);
    }
    return rawFrequencyKHz;
}

static GString* _utility_formatError(const gchar* file, gint line, const gchar* function, const gchar* message) {
    GString* errorString = g_string_new("**ERROR ENCOUNTERED**\n");
    g_string_append_printf(errorString, "\tAt process: %i (parent %i)\n", (gint) getpid(), (gint) getppid());
    g_string_append_printf(errorString, "\tAt file: %s\n", file);
    g_string_append_printf(errorString, "\tAt line: %i\n", line);
    g_string_append_printf(errorString, "\tAt function: %s\n", function);
    g_string_append_printf(errorString, "\tMessage: %s\n", message);
    return errorString;
}

static GString* _utility_formatBacktrace() {
    GString* backtraceString = g_string_new("**BEGIN BACKTRACE**\n");
    void *array[100];
    gsize size, i;
    gchar **strings;

    size = backtrace(array, 100);
    strings = backtrace_symbols(array, size);

    g_string_append_printf(backtraceString, "Obtained %zd stack frames:\n", size);

    for (i = 0; i < size; i++) {
        g_string_append_printf(backtraceString, "\t%s\n", strings[i]);
    }

    g_free(strings);
    g_string_append_printf(backtraceString, "**END BACKTRACE**\n");
    return backtraceString;
}

void utility_handleError(const gchar* file, gint line, const gchar* function, const gchar* message) {
    GString* errorString = _utility_formatError(file, line, function, message);
    GString* backtraceString = _utility_formatBacktrace();
    if(!isatty(fileno(stdout))) {
        g_print("%s%s**ABORTING**\n", errorString->str, backtraceString->str);
    }
    g_printerr("%s%s**ABORTING**\n", errorString->str, backtraceString->str);
    g_string_free(errorString, TRUE);
    g_string_free(backtraceString, TRUE);
    abort();
}

gboolean utility_isRandomPath(const gchar* path) {
    if(path) {
        return !g_ascii_strcasecmp(path, "/dev/random") ||
           !g_ascii_strcasecmp(path, "/dev/urandom") ||
           !g_ascii_strcasecmp(path, "/dev/srandom");
    } else {
        return FALSE;
    }
}

gboolean utility_removeAll(const gchar* path) {
    if(!path || !g_file_test(path, G_FILE_TEST_EXISTS)) {
        return FALSE;
    }

    gboolean isSuccess = TRUE;

    /* directories must be empty before we can remove them */
    if(g_file_test(path, G_FILE_TEST_IS_DIR)) {
        /* recurse into this directory and remove all files */
        GError* err = NULL;
        GDir* dir = g_dir_open(path, 0, &err);

        if(err) {
            warning("unable to open directory '%s': error %i: %s", path, err->code, err->message);
            isSuccess = FALSE;
            g_error_free(err);
        } else {
            const gchar* entry = NULL;
            while((entry = g_dir_read_name(dir)) != NULL) {
                gchar* childPath = g_build_filename(path, entry, NULL);
                gboolean childSuccess = utility_removeAll(childPath);
                g_free(childPath);
                if(!childSuccess) {
                    isSuccess = FALSE;
                    break;
                }
            }

            g_dir_close(dir);
        }
    }

    /* now remove the empty directory, or the file */
    if(g_remove(path) != 0) {
        warning("unable to remove path '%s': error %i: %s", path, errno, strerror(errno));
        isSuccess = FALSE;
    } else {
        info("removed path '%s' from filesystem", path);
        isSuccess = TRUE;
    }

    return isSuccess;
}

/* destructive copy that will remove dst path if it exists */
gboolean utility_copyAll(const gchar* srcPath, const gchar* dstPath) {
    if(!dstPath || !srcPath || !g_file_test(srcPath, G_FILE_TEST_EXISTS)) {
        return FALSE;
    }

    /* if destination already exists, delete it */
    if(g_file_test(dstPath, G_FILE_TEST_EXISTS) && !utility_removeAll(dstPath)) {
        return FALSE;
    }

    /* get file/dir mode */
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    if(g_lstat(srcPath, &statbuf) != 0) {
        warning("unable to stat src path '%s': error %i: %s", srcPath, errno, strerror(errno));
        return FALSE;
    }

    /* now create the dir or copy the file */
    if(g_file_test(srcPath, G_FILE_TEST_IS_DIR)) {
        /* create new dir with same mode as the old */
        if(g_mkdir(dstPath, statbuf.st_mode) != 0) {
            warning("unable to make dst path '%s': error %i: %s", dstPath, errno, strerror(errno));
            return FALSE;
        } else {
            /* now recurse into this directory */
            GError* err = NULL;
            GDir* dir = g_dir_open(srcPath, 0, &err);

            if(err) {
                warning("unable to open directory '%s': error %i: %s", srcPath, err->code, err->message);
                return FALSE;
            } else {
                gboolean isSuccess = TRUE;

                const gchar* entry = NULL;
                while((entry = g_dir_read_name(dir)) != NULL) {
                    gchar* srcChildPath = g_build_filename(srcPath, entry, NULL);
                    gchar* dstChildPath = g_build_filename(dstPath, entry, NULL);
                    isSuccess = utility_copyAll(srcChildPath, dstChildPath);
                    g_free(srcChildPath);
                    g_free(dstChildPath);
                    if(!isSuccess) {
                        break;
                    }
                }

                g_dir_close(dir);

                if(!isSuccess) {
                    return FALSE;
                }
            }
        }
    } else {
        gchar* srcContents = NULL;
        gsize srcLength = 0;
        GError* err = NULL;

        gboolean isSuccess = g_file_get_contents(srcPath, &srcContents, &srcLength, &err);

        if(isSuccess && !err) {
            isSuccess = g_file_set_contents(dstPath, srcContents, (gssize)srcLength, &err);
            if(isSuccess & !err) {
                info("copied path '%s' to '%s'", srcPath, dstPath);
            }
        }

        if(srcContents) {
            g_free(srcContents);
        }

        if(err) {
            warning("unable to read file '%s': error %i: %s", srcPath, err->code, err->message);
            g_error_free(err);
            return FALSE;
        }
    }

    if(g_chmod(dstPath, statbuf.st_mode) != 0) {
        warning("unable to chmod dst path '%s': error %i: %s", dstPath, errno, strerror(errno));
        return FALSE;
    }

    return TRUE;
}
