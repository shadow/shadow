/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_ACTION_H_
#define SHD_TGEN_ACTION_H_

#include "shd-tgen.h"

typedef enum _TGenActionType {
    TGEN_ACTION_START,
    TGEN_ACTION_END,
    TGEN_ACTION_PAUSE,
    TGEN_ACTION_TRANSFER,
    TGEN_ACTION_SYNCHR0NIZE,
} TGenActionType;

typedef struct _TGenAction TGenAction;

TGenAction* tgenaction_newStartAction(const gchar* timeStr, const gchar* serverPortStr,
		const gchar* peersStr, const gchar* socksProxyStr, GError** error);
TGenAction* tgenaction_newEndAction(const gchar* timeStr, const gchar* countStr,
		const gchar* sizeStr, GError** error);
TGenAction* tgenaction_newPauseAction(const gchar* timeStr, GError** error);
TGenAction* tgenaction_newSynchronizeAction(GError** error);
TGenAction* tgenaction_newTransferAction(const gchar* typeStr, const gchar* protocolStr,
		const gchar* sizeStr, const gchar* peersStr, GError** error);
void tgenaction_free(TGenAction* action);

void tgenaction_setKey(TGenAction* action, gpointer key);
gpointer tgenaction_getKey(TGenAction* action);

TGenActionType tgenaction_getType(TGenAction* action);
guint64 tgenaction_getServerPort(TGenAction* action);
TGenPeer tgenaction_getSocksProxy(TGenAction* action);
guint64 tgenaction_getStartTimeMillis(TGenAction* action);
guint64 tgenaction_getPauseTimeMillis(TGenAction* action);
void tgenaction_getTransferParameters(TGenAction* action, TGenTransferType* typeOut,
        TGenTransferProtocol* protocolOut, guint64* sizeOut);
TGenPool* tgenaction_getPeers(TGenAction* action);
guint64 tgenaction_getEndTimeMillis(TGenAction* action);
guint64 tgenaction_getEndCount(TGenAction* action);
guint64 tgenaction_getEndSize(TGenAction* action);

#endif /* SHD_TGEN_ACTION_H_ */
