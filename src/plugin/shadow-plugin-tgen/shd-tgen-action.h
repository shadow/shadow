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
    TGEN_ACTION_CHOOSE,
} TGenActionType;

typedef struct _TGenAction TGenAction;

TGenAction* tgenaction_newStartAction(const gchar* timeStr, const gchar* timeoutStr,
        const gchar* stalloutStr, const gchar* loglevelStr, const gchar* serverPortStr,
        const gchar* peersStr, const gchar* socksProxyStr, GError** error);
TGenAction* tgenaction_newEndAction(const gchar* timeStr, const gchar* countStr,
        const gchar* sizeStr, GError** error);
TGenAction* tgenaction_newPauseAction(const gchar* timeStr, GError** error);
TGenAction* tgenaction_newSynchronizeAction(glong totalIncoming, GError** error);
TGenAction* tgenaction_newTransferAction(const gchar* typeStr, const gchar* protocolStr,
        const gchar* sizeStr, const gchar* peersStr, const gchar* timeoutStr, const gchar* stalloutStr, GError** error);
TGenAction* tgenaction_newChooseAction(GError** error, gboolean hasWeights, gdouble totalWeight);

void tgenaction_ref(TGenAction* action);
void tgenaction_unref(TGenAction* action);

void tgenaction_setKey(TGenAction* action, gpointer key);
gpointer tgenaction_getKey(TGenAction* action);

TGenActionType tgenaction_getType(TGenAction* action);
guint16 tgenaction_getServerPort(TGenAction* action);
TGenPeer* tgenaction_getSocksProxy(TGenAction* action);
guint64 tgenaction_getStartTimeMillis(TGenAction* action);
guint64 tgenaction_getPauseTimeMillis(TGenAction* action);
guint64 tgenaction_getDefaultTimeoutMillis(TGenAction* action);
guint64 tgenaction_getDefaultStalloutMillis(TGenAction* action);
GLogLevelFlags tgenaction_getLogLevel(TGenAction* action);
void tgenaction_getTransferParameters(TGenAction* action, TGenTransferType* typeOut,
        TGenTransportProtocol* protocolOut, guint64* sizeOut, guint64* timeoutOut, guint64* stalloutOut);
TGenPool* tgenaction_getPeers(TGenAction* action);
guint64 tgenaction_getEndTimeMillis(TGenAction* action);
guint64 tgenaction_getEndCount(TGenAction* action);
guint64 tgenaction_getEndSize(TGenAction* action);

glong tgenaction_getTotalIncoming(TGenAction* action);
glong tgenaction_getCompletedIncoming(TGenAction* action);
void tgenaction_setCompletedIncoming(TGenAction* action, glong completedIncoming);

gboolean tgenaction_getHasWeights(TGenAction* action);
gdouble tgenaction_getTotalWeight(TGenAction* action);

#endif /* SHD_TGEN_ACTION_H_ */
