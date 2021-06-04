# Shadow Configuration

Shadow requires a configuration file that provides a network topology graph and information about the processes to run during the simulation. This configuration file uses the YAML format. The options and their effect on the simulation are described in more detail (alongside a simple example configuration file) on [the configuration options page](shadow_config_options.md).

Many of the configuration file options can also be overridden using command-line options. For example, the configuration option [`general.stop_time`](shadow_config_options.md#generalstop_time) can be overridden with shadow's `--stop-time` option, and [`general.log_level`](shadow_config_options.md#generallog_level) can be overridden with `--log-level`. See `shadow --help` for other command-line options.
