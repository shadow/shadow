/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_SERVER_H_
#define SHD_TGEN_SERVER_H_

typedef struct _TGenServer TGenServer;

typedef void (*TGenServer_onNewPeerFunc)(gpointer data, gint socketD, TGenPeer* peer);

TGenServer* tgenserver_new(TGenIO* io, in_port_t serverPort, TGenServer_onNewPeerFunc notify, gpointer notifyData);
void tgenserver_ref(TGenServer* server);
void tgenserver_unref(TGenServer* server);

#endif /* SHD_TGEN_SERVER_H_ */
