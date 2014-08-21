/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_ACTION_H_
#define SHD_TGEN_ACTION_H_

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

guint64 tgenaction_getServerPort(TGenAction* action);

#endif /* SHD_TGEN_ACTION_H_ */
