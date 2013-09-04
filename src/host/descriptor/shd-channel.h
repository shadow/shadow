/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CHANNEL_H_
#define SHD_CHANNEL_H_

#include "shadow.h"

enum ChannelType {
	CT_NONE, CT_READONLY, CT_WRITEONLY,
};

typedef struct _Channel Channel;

Channel* channel_new(gint handle, gint linkedHandle, enum ChannelType type);
gint channel_getLinkedHandle(Channel* channel);

#endif /* SHD_CHANNEL_H_ */
