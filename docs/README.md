# The Shadow Simulator Documentation

The docs contain important information about installing and using the Shadow
discrete-event network simulator. Please [open an
issue](https://github.com/shadow/shadow/issues) if you notice something out of
date, or [open a pull request](https://github.com/shadow/shadow/pulls) if you
can fix it.

 * The Shadow simulator
   * [Design Overview](design_2x.md)
   * Installation Guide
     * [Supported Platforms](supported_platforms.md)
     * [Dependencies](install_dependencies.md)
     * [Shadow](install_shadow.md)
     * [System Configuration](system_configuration.md)
     * [(Experimental) Shadow with Docker](install_shadow_with_docker.md)
   * Usage Guide
     * [Overview](run_shadow_overview.md)
     * Running Your First Simulations
       * [Basic File Transfer](getting_started_basic.md)
       * [Traffic Generation](getting_started_tgen.md)
       * [Simple Tor Network](getting_started_tor.md)
     * Understanding Shadow Output
       * [Format of the Log Messages](log_format.md)
       * [Parsing Statistics from the Logs](parsing_shadow_logs.md)
     * Configuring Your Own Simulation
       * [Shadow Config Overview](shadow_config_overview.md)
       * [Shadow Config Specification](shadow_config_spec.md)
     * Configuring Your Own Network
       * [Network Graph Overview](network_graph_overview.md)
       * [Network Graph Specification](network_graph_spec.md)
     * [Migrating Simulations from Shadow 1.x](migrating_from_1x.md)
   * Developer Guides
     * [Debugging and profiling](developer_guide.md)
     * [Continous integration tests](ci.md)
     * [Using a recompiled libc](using_recompiled_libc.md) to get higher libc
       API coverage using preload interposition.
 * The Shadow project
   * [Contributing](contributing.md)
     * [Coding style](coding_style.md)
     * [Pull requests](pull_requests.md)
   * [Maintainer playbook](maintainer_playbook.md)
   * [NSF Sponsorship](nsf_sponsorship.md)
