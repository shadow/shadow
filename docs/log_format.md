# Format of Shadow Log Messages

| ❗ Warning                                                                  |
|-----------------------------------------------------------------------------|
| The format of the log messages is not<br>stable and may change at any time. |

<br>

## Log Line Prefix

Shadow produces simulator log messages in the following format:

```text
real-time [thread-id:thread-name] virtual-time [loglevel] [hostname:ip] [src-file:line-number] [function-name] MESSAGE
```

- `real-time`:  
  the wall clock time since the start of the experiment, represented as
  `hours:minutes:seconds`
- `thread-id`:  
  the thread id (as returned by `gettid`) of the system thread that generated
  the message.
- `thread-name`:  
  the name of the system thread that generated the message
- `virtual-time`:  
  the simulated time since the start of the experiment, represented as
  `hours:minutes:seconds`
- `loglevel`:  
  one of `ERROR` < `WARN` < `INFO` < `DEBUG` < `TRACE`, in that order
- `hostname`:  
  the name of the host as specified in `hosts.<hostname>` of the simulation
  config
- `ip`:  
  the IP address of the host as specified in `hosts.<hostname>.ip_address_hint`
  of the simulation config, or a random IP address if one is not specified  
- `src-file`:  
  the name of the source code file where the message is logged
- `line-number`:  
  the line number in the source code file where the message is logged
- `function-name`:  
  the name of the function logging the message
- `MESSAGE`:  
  the actual message to be logged

By default, Shadow only prints core messages at or below the [`info` log
level](shadow_config_spec.md#generallog_level). This behavior can be changed
using the Shadow option `-l` or `--log-level` to increase or decrease the
verbosity of the output. As mentioned in the example from the previous section,
the output from each application process is stored in separate log files beneath
the `shadow.data` directory, and the format of those log files is
application-specific (i.e., Shadow writes application output _directly_ to
file).

## Heartbeat Messages

Shadow logs simulator heartbeat messages that contain useful system information.
By default, these heartbeats are logged once per second, but the frequency can be
changed using the `--heartbeat-frequency` option to Shadow (see `shadow --help`).
