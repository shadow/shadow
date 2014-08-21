/*
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenTransfer {
	guint64 getBytes;
	guint64 putBytes;
	gboolean isParallel;
	gboolean isRepeat;
	TGenPool* peerPool;
	guint magic;
};

TGenTransfer* tgentransfer_new(guint64 getBytes, guint64 putBytes,
		gboolean isParallel, gboolean isRepeat, TGenPool* peerPool) {
	TGenTransfer* transfer = g_new0(TGenTransfer, 1);
	transfer->magic = TGEN_MAGIC;

	transfer->getBytes = getBytes;
	transfer->putBytes = putBytes;
	transfer->isParallel = isParallel;
	transfer->isRepeat = isRepeat;

	/* peerPool may be NULL if we should use the session peerPool */
	transfer->peerPool = peerPool;

	return transfer;
}

void tgentransfer_free(TGenTransfer* transfer) {
	TGEN_ASSERT(transfer);

	if(transfer->peerPool) {
		tgenpool_free(transfer->peerPool);
	}

	transfer->magic = 0;
	g_free(transfer);
}

