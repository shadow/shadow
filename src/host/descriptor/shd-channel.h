/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CHANNEL_H_
#define SHD_CHANNEL_H_

#include "shadow.h"

typedef enum _ChannelType ChannelType;
enum _ChannelType {
    CT_NONE, CT_READONLY, CT_WRITEONLY,
};

typedef struct _Channel Channel;

Channel* channel_new(gint handle, ChannelType type);
void channel_setLinkedChannel(Channel* channel, Channel* linkedChannel);
Channel* channel_getLinkedChannel(Channel* channel);

#endif /* SHD_CHANNEL_H_ */
