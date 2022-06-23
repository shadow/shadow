/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib/gstdio.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/utility/utility.h"

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

static GString* _utility_formatError(const gchar* file, gint line, const gchar* function,
                                     const gchar* message, va_list vargs) {
    GString* errorString = g_string_new("**ERROR ENCOUNTERED**\n");
    g_string_append_printf(errorString, "\tAt process: %i (parent %i)\n", (gint) getpid(), (gint) getppid());
    g_string_append_printf(errorString, "\tAt file: %s\n", file);
    g_string_append_printf(errorString, "\tAt line: %i\n", line);
    g_string_append_printf(errorString, "\tAt function: %s\n", function);
    g_string_append_printf(errorString, "\tMessage: ");
    g_string_append_vprintf(errorString, message, vargs);
    g_string_append_printf(errorString, "\n");
    return errorString;
}

void utility_handleError(const gchar* file, gint line, const gchar* function, const gchar* message,
                         ...) {
    logger_flush(logger_getDefault());

    va_list vargs;
    va_start(vargs, message);
    GString* errorString = _utility_formatError(file, line, function, message, vargs);
    va_end(vargs);

    char* backtraceString = backtrace();

    if (!isatty(fileno(stdout))) {
        g_print("%s**BEGIN BACKTRACE**\n%s\n**END BACKTRACE**\n**ABORTING**\n", errorString->str,
                backtraceString);
    }
    g_printerr("%s**BEGIN BACKTRACE**\n%s\n**END BACKTRACE**\n**ABORTING**\n", errorString->str,
               backtraceString);

    g_string_free(errorString, TRUE);
    backtrace_free(backtraceString);
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
        debug("removed path '%s' from filesystem", path);
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
                debug("copied path '%s' to '%s'", srcPath, dstPath);
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

GString* utility_getFileContents(const gchar* fileName) {
    utility_assert(fileName);

    gchar* content;
    gsize length;
    GError *error = NULL;

    /* get the xml file */
    trace("attempting to get contents of file '%s'", fileName);
    gboolean success = g_file_get_contents(fileName, &content, &length, &error);
    trace("finished getting contents of file '%s'", fileName);

    /* check for success */
    if (!success) {
        warning("g_file_get_contents: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    GString* fileBuffer = g_string_new(content);
    g_free(content);
    return fileBuffer;
}

gchar* utility_getNewTemporaryFilename(const gchar* templateStr) {
    /* try to open a templated file, checking for errors */
    gchar* temporaryFilename = NULL;
    GError* error = NULL;

    gint openedFile = g_file_open_tmp(templateStr, &temporaryFilename, &error);
    if(openedFile < 0) {
        utility_panic("unable to open temporary file for cdata topology: %s", error->message);
        return NULL;
    }

    /* cleanup */
    close(openedFile);
    g_unlink(temporaryFilename);
    return temporaryFilename;
}

gboolean utility_copyFile(const gchar* fromPath, const gchar* toPath) {
    gchar* contents = NULL;
    gsize length = 0;
    GError* error = NULL;

    /* get the original file */
    if(!g_file_get_contents(fromPath, &contents, &length, &error)) {
        utility_panic("unable to read '%s' for copying: %s", fromPath, error->message);
        return FALSE;
    }
    error = NULL;

    /* copy to the new file */
    if(!g_file_set_contents(toPath, contents, (gssize)length, &error)) {
        utility_panic(
            "unable to write private copy of '%s' to '%s': %s", fromPath, toPath, error->message);
        return FALSE;
    }

    /* ok, our private copy was created, cleanup */
    g_free(contents);
    return TRUE;
}

gchar* utility_strvToNewStr(gchar** strv) {
    GString* strBuffer = g_string_new(NULL);

    if(strv) {
        for(gint i = 0; strv[i] != NULL; i++) {
            if(strv[i+1] == NULL) {
                g_string_append_printf(strBuffer, "%s", strv[i]);
            } else {
                g_string_append_printf(strBuffer, "%s ", strv[i]);
            }
        }
    }

    return g_string_free(strBuffer, FALSE);
}

struct timespec utility_timespecFromMillis(int64_t millis) {
    return (struct timespec){
        .tv_sec = millis / 1000,              // ms to sec
        .tv_nsec = (millis % 1000) * 1000000, // ms to ns
    };
}

int return_code_for_signal(int signal) {
    // To calculate the return code if the process exited by a signal,
    // follow the behaviour of bash and add 128 to to the signal.
    return signal + 128;
}

void die_after_vfork() {
    // Capture errno in a local, so that it can be examined in a stack trace.
    int saved_errno = errno;

    // Ensure our saved errno doesn't get optimized away.
    asm volatile(/*asm=*/"" : /*outputs=*/ : /*inputs=*/"irm"(saved_errno));

    // `abort` and `raise` are higher-level functions that could attempt to
    // access global memory, which could have surprising results. We resort to a
    // bare `kill`.
    kill(getpid(), SIGABRT);

    // Convince the compiler that we really don't return.
    _exit(1);
}
