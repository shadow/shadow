/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_POOL_H_
#define SHD_TGEN_POOL_H_

typedef struct _TGenPool TGenPool;

TGenPool* tgenpool_new();
void tgenpool_ref(TGenPool* pool);
void tgenpool_unref(TGenPool* pool);

void tgenpool_add(TGenPool* pool, gpointer item);
gconstpointer tgenpool_getRandom(TGenPool* pool);

#endif /* SHD_TGEN_POOL_H_ */
