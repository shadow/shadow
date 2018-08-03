/*
 * See LICENSE for licensing information
 */

#ifndef SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_MARKOVMODEL_H_
#define SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_MARKOVMODEL_H_

/* this is how many bytes we send for each packet type observation */
#define TGEN_MMODEL_PACKET_DATA_SIZE 1434
/* and packets sent within this many microseconds will be sent
 * at the same time for efficiency reasons */
#define TGEN_MMODEL_MICROS_AT_ONCE 1000

typedef enum _Observation Observation;
enum _Observation {
    OBSERVATION_PACKET_TO_SERVER,
    OBSERVATION_PACKET_TO_ORIGIN,
    OBSERVATION_STREAM,
    OBSERVATION_END,
};

typedef struct _TGenMarkovModel TGenMarkovModel;

TGenMarkovModel* tgenmarkovmodel_new(const gchar* modelPath);
void tgenmarkovmodel_ref(TGenMarkovModel* mmodel);
void tgenmarkovmodel_unref(TGenMarkovModel* mmodel);

Observation tgenmarkovmodel_getNextObservation(TGenMarkovModel* mmodel, guint64* delay);
void tgenmarkovmodel_reset(TGenMarkovModel* mmodel);

#endif /* SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_MARKOVMODEL_H_ */
