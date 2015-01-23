/*
 * See LICENSE for licensing information
 */

#ifndef SHD_TGEN_PEER_H_
#define SHD_TGEN_PEER_H_

#include "shd-tgen.h"

typedef struct _TGenPeer TGenPeer;

TGenPeer* tgenpeer_newFromName(const gchar* name, in_port_t networkPort);
TGenPeer* tgenpeer_newFromIP(in_addr_t networkIP, in_port_t networkPort);
void tgenpeer_ref(TGenPeer* peer);
void tgenpeer_unref(TGenPeer* peer);

void tgenpeer_performLookups(TGenPeer* peer);

in_addr_t tgenpeer_getNetworkIP(TGenPeer* peer);
in_port_t tgenpeer_getNetworkPort(TGenPeer* peer);
in_addr_t tgenpeer_getHostIP(TGenPeer* peer);
in_port_t tgenpeer_getHostPort(TGenPeer* peer);
const gchar* tgenpeer_getName(TGenPeer* peer);
const gchar* tgenpeer_toString(TGenPeer* peer);

#endif /* SHD_TGEN_PEER_H_ */
