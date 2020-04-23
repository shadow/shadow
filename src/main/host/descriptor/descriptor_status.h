/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_STATUS_H_
#define SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_STATUS_H_

/* Bitfield representing possible status types and their states. */
typedef enum _DescriptorStatus DescriptorStatus;
enum _DescriptorStatus {
    DS_NONE = 0,
    /* the descriptor has been initialized and it is now OK to
     * unblock any plugin waiting on a particular status  */
    DS_ACTIVE = 1 << 0,
    /* can be read, i.e. there is data waiting for user */
    DS_READABLE = 1 << 1,
    /* can be written, i.e. there is available buffer space */
    DS_WRITABLE = 1 << 2,
    /* user already called close */
    DS_CLOSED = 1 << 3,
};

#endif /* SRC_MAIN_HOST_DESCRIPTOR_SHD_DESCRIPTOR_STATUS_H_ */
