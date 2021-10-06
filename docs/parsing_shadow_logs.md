# Parsing Shadow Log Messages

| ❗ Warning                                                                                                  |
|-------------------------------------------------------------------------------------------------------------|
| The heartbeat/tracker log messages are considered experimental<br>and may change or be removed at any time. |

<br>

Shadow logs simulator heartbeat messages that contain useful system information
for each virtual host in the experiment. For example, Shadow logs the number of
bytes sent/received, number of bytes allocated/deallocated, CPU usage, etc. You
can parse these heartbeat log messages to get insight into the simulation.
Details of these heartbeat messages can be found
[here](log_format.md#heartbeat-messages), and they can be enabled by setting the
experimental
[`experimental.host_heartbeat_interval`](shadow_config_spec.md#experimentalhost_heartbeat_interval)
configuration option.
 
## Example Simulation Data

The methods we describe below can be used on the output from and Shadow
simulation. Here, we use the output from the [Traffic
Generation](getting_started_tgen.md) example simulation for illustrative
purposes.

## Parsing and Plotting Results

Shadow includes some Python scripts that can parse important statistics from the
Shadow log messages, including network throughput over time, client download
statistics, and client load statistics, and then visualize the results. The
following will parse and plot the output produced from the above experiment:

```bash
# parse the shadow output file
src/tools/parse-shadow.py --help
src/tools/parse-shadow.py --prefix results shadow.log
# plot the results
src/tools/plot-shadow.py --help
src/tools/plot-shadow.py --data results "example-plots"
```

The `parse-*.py` scripts generate `stats.*.json.xz` files. The (heavily trimmed)
contents of `stats.shadow.json` look like the following:

```text
$ xzcat results/stats.shadow.json.xz
{
  "nodes": {
    "client:11.0.0.1": {
      "recv": {
        "bytes_control_header": {
          "0": 0,
          "1": 0,
          "2": 0,
          ...
          "599": 0
        },
        "bytes_control_header_retrans": { ... },
        "bytes_data_header": { ... },
        "bytes_data_header_retrans": { ... },
        "bytes_data_payload": { ... },
        "bytes_data_payload_retrans": { ... },
        "bytes_total": { ... }
      },
      "send": { ... }
    },
    "server:11.0.0.2": { ... }
  },
  "ticks": {
    "2": {
      "maxrss_gib": 0.162216,
      "time_seconds": 0.070114
    },
    "3": { ... },
    ...
    "599": { ... }
  }
}
```

The `plot-*.py` scripts generate graphs. Open the PDF file that was created to
see the graphed results.

### Comparing Data from Multiple Simulations

Consider a set of experiments where we would like to analyze the effect of
changing our hosts' socket receive buffer sizes. We run the following 2
experiments:

```bash
# delete any existing simulation data and post-processing
rm -rf shadow.{data,log} 10KiB.{data,results,log} 100KiB.{data,results,log} *.results.pdf
shadow --socket-recv-buffer  10KiB --socket-recv-autotune false \
       --data-directory  10KiB.data shadow.yaml >  10KiB.log
shadow --socket-recv-buffer 100KiB --socket-recv-autotune false \
       --data-directory 100KiB.data shadow.yaml > 100KiB.log
```

To parse these log files, we use the following scripts:

```bash
src/tools/parse-shadow.py --prefix=10KiB.results   10KiB.log
src/tools/parse-shadow.py --prefix=100KiB.results 100KiB.log
```

Each of the directories `10KiB.results/` and `100KiB.results/` now contain data
statistics files extracted from the log files. We can now combine and visualize
these results with the `plot-shadow.py` script:

```bash
src/tools/plot-shadow.py --prefix "recv-buffer" --data 10KiB.results/ "10 KiB" --data 100KiB.results/ "100 KiB"
```

Open the PDF file that was created to compare results from the experiments.
