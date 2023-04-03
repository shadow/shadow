# Shadow Configuration Overview

Shadow requires a configuration file that provides a network graph and
information about the processes to run during the simulation. This configuration
file uses the YAML format. The options and their effect on the simulation are
described in more detail (alongside a simple example configuration file) on [the
configuration options page](shadow_config_spec.md).

Many of the configuration file options can also be overridden using command-line
options. For example, the configuration option
[`general.stop_time`](shadow_config_spec.md#generalstop_time) can be
overridden with shadow's `--stop-time` option, and
[`general.log_level`](shadow_config_spec.md#generallog_level) can be
overridden with `--log-level`. See `shadow --help` for other command-line
options.

## Quantities with Units

Some options such as
[`hosts.<hostname>.bandwidth_down`](shadow_config_spec.md#hostshostnamebandwidth_down)
accept quantity values containing a magnitude and a unit. For example bandwidth
values can be expressed as `1 Mbit`, `1000 Kbit`, `977 Kibit`, etc. The space
between the magnitude and unit is optional (for example `5Mbit`), and the unit
can be pluralized (for example `5 Mbits`). Units are case-sensitive.

### Time

Time values are expressed as either sub-second units, seconds, minutes, or
hours. Not all options will accept sub-second units. For example
[`general.stop_time`](shadow_config_spec.md#generalstop_time) must be
expressed in units of seconds or larger.

Acceptable units are:

- nanosecond / ns
- microsecond / us / μs
- millisecond / ms
- second / sec / s
- minute / min / m
- hour / hr / h

Examples: `30 s`, `2 hr`, `10 minutes`, `100 ms`

### Bandwidth

Bandwidth values are expressed in bits-per-second with the unit `bit`. All
bandwidth values should be divisible by 8 bits-per-second (for example `30 bit`
is invalid, but `30 Kbit` is valid).

Acceptable unit *prefixes* are:

- kilo / K
- kibi / Ki
- mega / M
- mebi / Mi
- giga / G
- gibi / Gi
- tera / T
- tebi / Ti

Examples: `100 Mbit`, `100 Mbits`, `10 kilobits`, `128 bits`

### Byte Sizes

Byte size values are expressed with the unit `byte` or `B`.

Acceptable unit *prefixes* are:

- kilo / K
- kibi / Ki
- mega / M
- mebi / Mi
- giga / G
- gibi / Gi
- tera / T
- tebi / Ti

Examples: `20 B`, `100 MB`, `100 megabyte`, `10 kibibytes`, `30 MiB`, `1024 Mbytes`

## YAML Extensions

Shadow supports the extended YAML conventions for [merge
keys](https://yaml.org/type/merge.html) and [extension
fields](https://docs.docker.com/compose/compose-file/#extension)).

For examples, see [Managing Complex Configurations](./shadow_config_complex.md).