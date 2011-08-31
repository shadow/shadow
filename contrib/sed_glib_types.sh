#!/bin/bash

for f in `find src -name '*.c' -or -name '*.h'`
do
    echo $f

# replace basic types
    sed -i 's/void\*/gpointer /g' $f
    sed -i 's/void \*/gpointer /g' $f
    sed -i 's/gpointer  /gpointer /g' $f
    sed -i 's/unsigned char/guchar/g' $f
    sed -i 's/unsigned int/guint/g' $f
    sed -i 's/char/gchar/g' $f
    sed -i 's/double/gdouble/g' $f
    sed -i 's/uint8_t/guint8/g' $f
    sed -i 's/uint16_t/guint16/g' $f
    sed -i 's/uint32_t/guint32/g' $f
    sed -i 's/uint64_t/guint64/g' $f
    sed -i 's/int8_t/gint8/g' $f
    sed -i 's/int16_t/gint16/g' $f
    sed -i 's/int32_t/gint32/g' $f
    sed -i 's/int64_t/gint64/g' $f
    sed -i 's/int/gint/g' $f

# fix up incorrect replacements
    sed -i 's/guguint/guint/g' $f
    sed -i 's/gugint/guint/g' $f
    sed -i 's/ggint/gint/g' $f
    sed -i 's/stdgint/stdint/g' $f
    sed -i 's/gpoginter/gpointer/g' $f
    sed -i 's/g_gint_equal/g_int_equal/g' $f
    sed -i 's/g_gint_hash/g_int_hash/g' $f
    sed -i 's/guguchar/guchar/g' $f
    sed -i 's/gugchar/guchar/g' $f
    sed -i 's/_gintercept/_intercept/g' $f
    sed -i 's/_ginternal/_internal/g' $f
    sed -i 's/gintercept/intercept/g' $f
    sed -i 's/prgintf/printf/g' $f
    sed -i 's/ginterface/interface/g' $f
    sed -i 's/_gdouble/_double/g' $f

# add glib include to all files
    sed -i '0,/\#include/s/\#include/\#include <glib.h>\n\#include/' $f
done


