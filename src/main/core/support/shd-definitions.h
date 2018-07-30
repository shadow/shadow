/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_DEFINITIONS_H_
#define SHD_DEFINITIONS_H_

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
/* #define EMULATED_TIME_OFFSET (G_GUINT64_CONSTANT(946684800) * SIMTIME_ONE_SECOND) */
#define EMULATED_TIME_OFFSET (G_GUINT64_CONSTANT(1531792888) * SIMTIME_ONE_SECOND)

/**
 * Conversion from emulated time to simulated time.
 */
#define EMULATED_TIME_TO_SIMULATED_TIME(emtime) ((SimulationTime)(emtime-EMULATED_TIME_OFFSET))

#ifdef DEBUG
/**
 * Memory magic for assertions that memory has not been freed. The idea behind
 * this approach is to declare a value in each struct using MAGIC_DECLARE,
 * define it using MAGIC_INIT during object creation, and clear it during
 * cleanup using MAGIC_CLEAR. Any time the object is referenced, we can check
 * the magic value using MAGIC_ASSERT. If the assert fails, there is a bug.
 *
 * In general, this should only be used in DEBUG mode. Once we are somewhat
 * convinced on Shadow's stability (for releases), these macros will do nothing.
 *
 * MAGIC_VALUE is an arbitrary value.
 *
 * @todo add #ifdef DEBUG
 */
#define MAGIC_VALUE 0xAABBCCDD

/**
 * Declare a member of a struct to hold a MAGIC_VALUE. This should be placed in
 * the declaration of a struct, generally as the last member of the struct.
 */
#define MAGIC_DECLARE guint magic

/**
 * Initialize a value declared with MAGIC_DECLARE to MAGIC_VALUE
 */
#define MAGIC_INIT(object) object->magic = MAGIC_VALUE

/**
 * Assert that a struct declared with MAGIC_DECLARE and initialized with
 * MAGIC_INIT still holds the value MAGIC_VALUE.
 */
#define MAGIC_ASSERT(object) utility_assert(object && (object->magic == MAGIC_VALUE))

/**
 * CLear a magic value. Future assertions with MAGIC_ASSERT will fail.
 */
#define MAGIC_CLEAR(object) object->magic = 0
#else
#define MAGIC_VALUE
#define MAGIC_DECLARE
#define MAGIC_INIT(object)
#define MAGIC_ASSERT(object)
#define MAGIC_CLEAR(object)
#endif

/**
 * The minimum file descriptor shadow returns to the plugin.
 */
#define MIN_DESCRIPTOR 10

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
 */
#define CONFIG_TCP_RTO_INIT 1000
#define CONFIG_TCP_RTO_MIN 200
#define CONFIG_TCP_RTO_MAX 1200000

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
