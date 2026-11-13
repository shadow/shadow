# qperf

The `qperf` binary is a network performance benchmarking tool specifically
designed to measure the throughput and behavior of the QUIC protocol over UDP.

## Use Case

It functions as a coordinated Client/Server pair that establishes a QUIC
connection, opens a stream, and transfers a specific amount of data (payload) to
stress-test the underlying transport layer.

It can be used to:

1. Verify QUIC Implementation: Ensure that a custom QUIC stack (i.e., quiche) is
   correctly handling stream flow control and congestion when running in Shadow.

2. Benchmark Network Stability: Measure how the QUIC protocol performs over
   different simulated network conditions.

3. Debug Protocol State: Using the qlog files to see exactly how the handshake
   and stream-opening processes are behaving under load.

## Capabilities

Full-Duplex Data Transfer: It uses a client and server that both send data to
each other while transitioning through a state machine (Handshake -> Identify ->
ReadWrite -> Close). QUIC is configured to use the CUBIC congestion control
algorithm during these transfers.

Configurable Payloads: Users can specify the exact number of bytes to be
transferred (--num-bytes) from the client and from the server, allowing for
precise benchmarking of different data sizes. 

Async I/O: Built on top of smol and quiche, it uses non-blocking I/O to handle
concurrent network events (sending, receiving, and timeouts) efficiently.

Performance Logging (qlogs): A critical feature of this tool is the
`enable_quic_qlog` function. It automatically captures QUIC-specific performance
metrics (like packet loss, congestion control behavior, and latency) into a
.sqlog file (a standard QUIC performance log file) for later analysis with the
`qperf_check` tool.

# qperf_check

This tool is a validation utility designed to verify that a QUIC connection
meets specific performance requirements by analyzing a standard sqlog file (the
output produced by the qperf binary).

## Workflow

1. Run qperf: Generate an sqlog file containing raw QUIC metrics.

2. Run this check tool: Provide the sqlog file and your "Pass/Fail" requirements
   (e.g., --rtt_med_ms 50 --bw_up_mbps 100).

3. Result: The tool parses the thousands of lines in the log, aggregates the
   stats, and tells you if the performance meets your requirements.

## Verfication Metrics

The tool automates the process of "grading" a performance test by comparing
actual metrics against expected thresholds. Specifically, it validates the
following:

1. Latency & Reliability (RTT & Loss)

- RTT Check: It calculates the Minimum and Median Round-Trip Times (RTT) from
  the log. It ensures they are within a specified tolerance (e.g., "Ensure
  median RTT is 50ms +/- 10ms").

- Packet Loss: It calculates the percentage of lost packets and verifies it
  doesn't exceed your allowed threshold.

2. Throughput (Goodput)

- Bandwidth Validation: It calculates the Goodput (the actual application-layer
  data successfully transferred) for both Upload and Download.

- Capacity Check: It verifies that the achieved Mbit/s is above a minimum floor
  (e.g., "Ensure we hit at least 100 Mbps").

3. Protocol Integrity (Congestion Control)

- Flow Control Validation: It ensures that the bytes_in_flight do not exceed the
  congestion_window beyond a specific tolerance. This is a critical check to
  ensure the QUIC implementation isn't over-sending data and violating the
  protocol.

- Verification of Events: It ensures that "success" events actually happened
  (e.g., verifying that packets were actually sent and received, rather than
  just timing out).

4. Automated Reporting

- Pass/Fail Status: Instead of a human looking at a log file, this tool returns
  a non-zero exit code and a FAIL ❌ message if any metric (RTT, Loss, or
  Bandwidth) falls outside of the defined tolerances.

- Detailed Diagnostics: If a check fails, it prints the exact expected vs.
  actual values to stderr, making it easy to identify which specific metric
  failed the test.

