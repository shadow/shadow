/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_UTILITY_H_
#define SHD_UTILITY_H_

#include <glib.h>
#include <stdio.h>
#include <netinet/in.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/core/support/definitions.h"

#define utility_alwaysAssert(expr)                                                                 \
    do {                                                                                           \
        if G_LIKELY (expr) {                                                                       \
            ;                                                                                      \
        } else {                                                                                   \
            utility_handleError(__FILE__, __LINE__, G_STRFUNC, "Assertion failed: %s", #expr);     \
        }                                                                                          \
    } while (0)

#ifdef DEBUG
#define utility_debugAssert(expr) utility_alwaysAssert(expr)
#else
#define utility_debugAssert(expr)
#endif

#define utility_panic(...) utility_handleError(__FILE__, __LINE__, G_STRFUNC, __VA_ARGS__);

#ifdef DEBUG
/**
 * Memory magic for assertions that memory has not been freed. The idea behind
 * this approach is to declare a value in each struct using MAGIC_DECLARE,
 * define it using MAGIC_INIT during object creation, and clear it during
 * cleanup using MAGIC_CLEAR. Any time the object is referenced, we can check
 * the magic value using MAGIC_ASSERT. If the assert fails, there is a bug.
 *
 * In general, this should only be used in DEBUG mode. Once we are somewhat
 * convinced on Shadow's stability (for releases), these macros will do nothing.
 *
 * MAGIC_VALUE is an arbitrary value.
 *
 * @todo add #ifdef DEBUG
 */
#define MAGIC_VALUE 0xAABBCCDD

/**
 * Declare a member of a struct to hold a MAGIC_VALUE. This should be placed in
 * the declaration of a struct, generally as the last member of the struct. If
 * the struct needs to have the same size in both debug and release mode, it
 * can use MAGIC_DECLARE_ALWAYS.
 */
#define MAGIC_DECLARE        guint magic
#define MAGIC_DECLARE_ALWAYS guint magic

/**
 * Initialize a value declared with MAGIC_DECLARE to MAGIC_VALUE. This is useful
 * for initializing the magic value in a struct initializer.
 */
#define MAGIC_INITIALIZER .magic = MAGIC_VALUE,

/**
 * Initialize a value declared with MAGIC_DECLARE to MAGIC_VALUE. This is useful
 * for initializing the magic value in an independent statement.
 */
#define MAGIC_INIT(object) (object)->magic = MAGIC_VALUE

/**
 * Assert that a struct declared with MAGIC_DECLARE and initialized with
 * MAGIC_INIT still holds the value MAGIC_VALUE.
 */
#define MAGIC_ASSERT(object) utility_debugAssert((object) && ((object)->magic == MAGIC_VALUE))

/**
 * CLear a magic value. Future assertions with MAGIC_ASSERT will fail.
 */
#define MAGIC_CLEAR(object) object->magic = 0
#else
#define MAGIC_VALUE
#define MAGIC_DECLARE
#define MAGIC_DECLARE_ALWAYS guint magic
#define MAGIC_INITIALIZER
#define MAGIC_INIT(object)
#define MAGIC_ASSERT(object)
#define MAGIC_CLEAR(object)
#endif

gboolean utility_isRandomPath(const gchar* path);

gchar* utility_strvToNewStr(gchar** strv);

__attribute__((__format__(__printf__, 4, 5)))
_Noreturn void utility_handleError(const gchar* file, gint line, const gchar* funtcion,
                                   const gchar* message, ...);

#endif /* SHD_UTILITY_H_ */
