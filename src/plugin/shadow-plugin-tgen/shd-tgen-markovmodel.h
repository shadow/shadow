/*
 * See LICENSE for licensing information
 */

#ifndef SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_MARKOVMODEL_H_
#define SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_MARKOVMODEL_H_

/* this is how many bytes we send for each packet type observation */
#define TGEN_MMODEL_PACKET_DATA_SIZE 1434
/* and packets sent within this many microseconds will be sent
 * at the same time for efficiency reasons */
#define TGEN_MMODEL_MICROS_AT_ONCE 100

typedef struct _TGenMarkovModel TGenMarkovModel;

TGenMarkovModel* tgenmarkovmodel_new(const gchar* modelPath);
void tgenmarkovmodel_ref(TGenMarkovModel* mmodel);
void tgenmarkovmodel_unref(TGenMarkovModel* mmodel);


#endif /* SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_MARKOVMODEL_H_ */
