/*
 * See LICENSE for licensing information
 */

#ifndef SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_GENERATOR_H_
#define SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_GENERATOR_H_

typedef struct _TGenGenerator TGenGenerator;

TGenGenerator* tgengenerator_new(const gchar* streamModelPath, const gchar* packetModelPath, TGenPool* peers);
void tgengenerator_ref(TGenGenerator* gen);
void tgengenerator_unref(TGenGenerator* gen);

gboolean tgengenerator_nextStream(TGenGenerator* gen);

#endif /* SRC_PLUGIN_SHADOW_PLUGIN_TGEN_SHD_TGEN_GENERATOR_H_ */
