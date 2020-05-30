/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_DEFINITIONS_H_
#define SHD_DEFINITIONS_H_

#include <glib.h>

// TODO put into a shd-types.h file
typedef struct _Process Process;

/**
 * Simulation time in nanoseconds. Allows for a consistent representation
 * of time throughput the simulator.
 */
typedef guint64 SimulationTime;

/**
 * Unique object id reference
 */
typedef guint ShadowID;

/**
 * Represents an invalid simulation time.
 */
#define SIMTIME_INVALID G_MAXUINT64

/**
 * Maximum and minimum valid values.
 */
#define SIMTIME_MAX (G_MAXUINT64-1)
#define SIMTIME_MIN 0

/**
 * Represents one nanosecond in simulation time.
 */
#define SIMTIME_ONE_NANOSECOND G_GUINT64_CONSTANT(1)

/**
 * Represents one microsecond in simulation time.
 */
#define SIMTIME_ONE_MICROSECOND G_GUINT64_CONSTANT(1000)

/**
 * Represents one millisecond in simulation time.
 */
#define SIMTIME_ONE_MILLISECOND G_GUINT64_CONSTANT(1000000)

/**
 * Represents one second in simulation time.
 */
#define SIMTIME_ONE_SECOND G_GUINT64_CONSTANT(1000000000)

/**
 * Represents one minute in simulation time.
 */
#define SIMTIME_ONE_MINUTE G_GUINT64_CONSTANT(60000000000)

/**
 * Represents one hour in simulation time.
 */
#define SIMTIME_ONE_HOUR G_GUINT64_CONSTANT(3600000000000)

/**
 * Emulation time in nanoseconds. Allows for a consistent representation
 * of time throughput the simulator. Emulation time is the simulation time
 * plus the EMULATION_TIME_OFFSET. This type allows us to explicitly
 * distinguish each type of time in the code.,
 */
typedef guint64 EmulatedTime;

/**
 * The number of nanoseconds from the epoch to January 1st, 2000 at 12:00am UTC.
 * This is used to emulate to applications that we are in a recent time.
 */
#define EMULATED_TIME_OFFSET (G_GUINT64_CONSTANT(946684800) * SIMTIME_ONE_SECOND)

/**
 * Conversion from emulated time to simulated time.
 */
#define EMULATED_TIME_TO_SIMULATED_TIME(emtime) ((SimulationTime)(emtime-EMULATED_TIME_OFFSET))

/**
 * The minimum file descriptor shadow returns to the plugin.
 * TODO: this is set high so that the FDs returned to the plugin by the OS
 * (e.g., when handling files) do not conflict with the FDs returned by
 * shadow. Change this to 3 when we properly handle all descriptor types.
 */
#define MIN_DESCRIPTOR 100

/**
 * The start of our random port range in host order, used if application doesn't
 * specify the port it wants to bind to, and for client connections.
 */
#define MIN_RANDOM_PORT 10000

/**
 * We always use TCP_autotuning unless this is set to FALSE
 *
 * @todo change this to a command line option accessible via #Configuration
 */
#define CONFIG_TCPAUTOTUNE TRUE

/**
 * Minimum, default, and maximum values for TCP send and receive buffers
 * Normally specified in:
 *      /proc/sys/net/ipv4/tcp_rmem
 *      /proc/sys/net/ipv4/tcp_wmem
 */
#define CONFIG_TCP_WMEM_MIN 4096
#define CONFIG_TCP_WMEM_DEFAULT 16384
#define CONFIG_TCP_WMEM_MAX 4194304
#define CONFIG_TCP_RMEM_MIN 4096
#define CONFIG_TCP_RMEM_DEFAULT 87380
#define CONFIG_TCP_RMEM_MAX 6291456

/**
 * Default initial retransmission timeout and ranges,
 * TCP_TIMEOUT_INIT=1000ms, TCP_RTO_MIN=200ms and TCP_RTO_MAX=120000ms from net/tcp.h
 *
 * HZ is about 1 second, i.e., about 1000 milliseconds
 */
#define NET_TCP_HZ 1000
#define CONFIG_TCP_RTO_INIT NET_TCP_HZ
#define CONFIG_TCP_RTO_MIN NET_TCP_HZ/5
#define CONFIG_TCP_RTO_MAX NET_TCP_HZ*120

/**
 * Default delay ack times, from net/tcp.h
 */
#define CONFIG_TCP_DELACK_MIN NET_TCP_HZ/25
#define CONFIG_TCP_DELACK_MAX NET_TCP_HZ/5

/**
 * Minimum size of the send buffer per socket when TCP-autotuning is used.
 * This value was computed from "man tcp"
 *
 * @todo change this to a command line option accessible via #Configuration
 */
#define CONFIG_SEND_BUFFER_MIN_SIZE 16384

/**
 * Minimum size of the receive buffer per socket when TCP-autotuning is used.
 * This value was computed from "man tcp"
 *
 * @todo change this to a command line option accessible via #Configuration
 */
#define CONFIG_RECV_BUFFER_MIN_SIZE 87380

/**
 * Default size of the send buffer per socket if TCP-autotuning is not used.
 * This value was computed from "man tcp"
 */
#define CONFIG_SEND_BUFFER_SIZE 131072

/**
 * Default size of the receive buffer per socket if TCP-autotuning is not used
 * This value was computed from "man tcp"
 */
#define CONFIG_RECV_BUFFER_SIZE 174760

/**
 * Default size for pipes. Value taken from "man 7 pipe".
 */
#define CONFIG_PIPE_BUFFER_SIZE 65536

/**
 * Default batching time when the network interface receives packets
 */
#define CONFIG_RECEIVE_BATCH_TIME (10*SIMTIME_ONE_MILLISECOND)

/**
 * Header size of a packet with UDP encapsulation
 * 14 bytes eth2, 20 bytes IP, 8 bytes UDP
 * Measured using wireshark on normal traffic.
 */
#define CONFIG_HEADER_SIZE_UDPIPETH 42

/**
 * Header size of a packet with TCP encapsulation
 * 14 bytes eth2, 20 bytes IP, 32 bytes UDP
 * Measured using wireshark on normal traffic.
 */
#define CONFIG_HEADER_SIZE_TCPIPETH 66

/**
 * Maximum size of an IP packet without fragmenting over Ethernetv2
 */
#define CONFIG_MTU 1500

/**
 * Maximum size of a datagram we are allowed to send out over the network
 */
#define CONFIG_DATAGRAM_MAX_SIZE 65507

/**
 * Delay in nanoseconds for a TCP close timer.
 */
#define CONFIG_TCPCLOSETIMER_DELAY (60 * SIMTIME_ONE_SECOND)

/**
 * Filename to find the CPU speed.
 */
#define CONFIG_CPU_MAX_FREQ_FILE "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"

#endif /* SHD_DEFINITIONS_H_ */
