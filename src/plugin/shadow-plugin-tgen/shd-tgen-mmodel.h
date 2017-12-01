/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_MMODEL_H_
#define SHD_TGEN_MMODEL_H_

#include <glib.h>

#define TGEN_MMODEL_PACKET_DATA_SIZE 1434
#define TGEN_MMODEL_MICROS_AT_ONCE 100

typedef struct _TGenMModel TGenMModel;

TGenMModel *tgenmmodel_new(const gchar *mmodelPath);
void tgenmmodel_ref(TGenMModel *mmodel);
void tgenmmodel_unref(TGenMModel *mmodel);

gboolean tgenmmodel_generatePath(TGenMModel *mmodel, GString *ourStr, GString *theirStr);

#endif /* SHD_TGEN_MMODEL_H_ */
