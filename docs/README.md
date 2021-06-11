# The Shadow Simulator Documentation

The docs contain important information about installing and using the Shadow
discrete-event network simulator. Please [open an
issue](https://github.com/shadow/shadow/issues) if you notice something out of
date, or [open a pull request](https://github.com/shadow/shadow/pulls) if you
can fix it.

 * The Shadow simulator
   * [Design Overview](design_overview.md)
   * Installation Guide
     * [Supported Platforms](supported_platforms.md)
     * [Dependencies](install_dependencies.md)
     * [Shadow](install_shadow.md)
     * [System Configuration](system_configuration.md)
     * [(Experimental) Shadow with Docker](install_shadow_with_docker.md)
   * Getting Started
     * [Basic File Transfer](getting_started_basic.md)
     * [Simple Tor Network](getting_started_tor.md)
   * [Parsing Statistics](parsing_statistics.md)
   * [Log Format](log_format.md)
   * Simulation Customization
     * [Shadow Configuration](shadow_config.md)
       * [Shadow Configuration Options](shadow_config_options.md)
     * [Network Configuration](network_config.md)
       * [Network Graph Attributes](network_graph_attributes.md)
   * [Migrating Simulations from Shadow 1.x](migrating_from_1x.md)
   * [Notes and FAQs](notes_and_faq.md)
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
