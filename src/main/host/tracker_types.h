/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TRACKER_TYPES_H_
#define SHD_TRACKER_TYPES_H_

typedef struct _Tracker Tracker;

typedef enum _LogInfoFlags LogInfoFlags;
enum _LogInfoFlags {
    LOG_INFO_FLAGS_NONE = 0,
    LOG_INFO_FLAGS_NODE = 1 << 0,
    LOG_INFO_FLAGS_SOCKET = 1 << 1,
    LOG_INFO_FLAGS_RAM = 1 << 2,
};

#endif /* SHD_TRACKER_TYPES_H_ */
