/*
 * See LICENSE for licensing information
 */

#ifndef SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_GENERATOR_H_
#define SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_GENERATOR_H_

typedef struct _TGenGenerator TGenGenerator;

TGenGenerator* tgengenerator_new(const gchar* streamModelPath, const gchar* packetModelPath,
        TGenAction* generateAction);
void tgengenerator_ref(TGenGenerator* gen);
void tgengenerator_unref(TGenGenerator* gen);

gboolean tgengenerator_generateStream(TGenGenerator* gen,
        gchar** localSchedule, gchar** remoteSchedule, guint64* pauseTimeUSec);

TGenAction* tgengenerator_getGenerateAction(TGenGenerator* gen);
void tgengenerator_onTransferCreated(TGenGenerator* gen);
void tgengenerator_onTransferCompleted(TGenGenerator* gen);
gboolean tgengenerator_isDoneGenerating(TGenGenerator* gen);

guint tgengenerator_getNumOutstandingTransfers(TGenGenerator* gen);
guint tgengenerator_getNumStreamsGenerated(TGenGenerator* gen);
guint tgengenerator_getNumPacketsGenerated(TGenGenerator* gen);

#endif /* SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_GENERATOR_H_ */
