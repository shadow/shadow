# Testing for Nondeterminism

If you run Shadow twice with the same seed (the `-s` or `--seed` command line
options), then it _should_ produce deterministic results (it's a bug if it
doesn't).

If you find non-deterministic behavior in your Shadow experiment, please
consider helping to diagnose the problem by opening a [new
issue](https://github.com/shadow/shadow/issues/new).

## Comparing strace output (experimental)

Shadow has an experimental feature for logging most system calls made by the
managed process in a format similar to the strace tool. You can enable this
with the [`strace_logging_mode` option][strace-logging-mode]. You can compare
this strace log from two simulations to look for non-deterministic behaviour.
To avoid capturing memory addresses and uninitialized memory in the log, you
should use the `deterministic` logging mode.

For example, after running two simulations with `--strace-logging-mode
deterministic` where the results are in the `shadow.data.1` and `shadow.data.2`
directories, you could run something like the following bash script:

[strace-logging-mode]: shadow_config_spec.md#experimentalstrace_logging_mode

```bash
#!/bin/bash

found_difference=0

for SUFFIX in \
    hosts/fileserver/tgen.1000.strace \
    hosts/client/tgen.1000.strace
do
    diff --brief shadow.data.1/${SUFFIX} shadow.data.2/${SUFFIX}
    exit_code=$?

    if (($exit_code != 0)); then
      found_difference=1
    fi
done

if (($found_difference == 1)); then
  echo -e "\033[0;31mDetected difference in output (Shadow may be non-deterministic).\033[0m"
else
  echo -e "\033[0;32mDid not detect difference in Shadow output (Shadow may be deterministic).\033[0m"
fi
```

## Comparing application output

A good way to check this is to compare the log output of an application that
was run in Shadow. For example, after running two TGen simulations where the
results are in the `shadow.data.1` and `shadow.data.2` directories, you could
run something like the following bash script:

```bash
#!/bin/bash

found_difference=0

for SUFFIX in \
    hosts/fileserver/tgen.1000.stdout \
    hosts/client/tgen.1000.stdout
do
    ## ignore memory addresses in log file with `sed 's/0x[0-9a-f]*/HEX/g' FILENAME`
    sed -i 's/0x[0-9a-f]*/HEX/g' shadow.data.1/${SUFFIX}
    sed -i 's/0x[0-9a-f]*/HEX/g' shadow.data.2/${SUFFIX}

    diff --brief shadow.data.1/${SUFFIX} shadow.data.2/${SUFFIX}
    exit_code=$?

    if (($exit_code != 0)); then
      found_difference=1
    fi
done

if (($found_difference == 1)); then
  echo -e "\033[0;31mDetected difference in output (Shadow may be non-deterministic).\033[0m"
else
  echo -e "\033[0;32mDid not detect difference in Shadow output (Shadow may be deterministic).\033[0m"
fi
```
