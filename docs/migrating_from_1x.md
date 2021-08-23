# Migrating Simulations from Shadow 1.x

Shadow 2.0 has changed the formatting of its configuration and network graph files.
The configuration format has changed from XML to YAML, and the network graph format
has changed from GraphML to GML. Various options have been added, renamed, and
removed. Shadow includes convenience scripts to aid in converting these files to their
new formats, but the generated output of these scripts requires manual user
intervention. For example, you will need to assign hosts to graph nodes manually by
setting the `network_node_id` field for each host in the generated config. You should
also manually compare the original file to the new converted file to make sure it
includes all of the options you expect.

When encountering a tag/attribute that is not supported by the new format, these
scripts will either:

1. Copy it to the new file anyways. When attempting to use this new file with
   Shadow, Shadow will raise an error for this unexpected field.
2. Ignore it and output a warning.

## Converting a configuration file to the Shadow 2.0 format

The following will create a new configuration file `my-shadow-config.yaml`.

```bash
shadow/src/tools/convert_legacy_config.py my-shadow-config.xml
```

You may have to manually tweak the new configuration file to support the new
virtual process [working
directory](https://en.wikipedia.org/wiki/Working_directory). In Shadow 1.x, each
virtual process (then called a plugin) has the same working directory as the
Shadow process itself. In Shadow 2.x, the working directory of each virtual
process is its host data directory. For example a process running on host
`myhost` would have the working directory `shadow.data/hosts/myhost/`. You can
use the
[`experimental.use_legacy_working_dir`](shadow_config_spec.md#experimentaluse_legacy_working_dir)
option to use the Shadow 1.x working directory, but this is an experimental
option and may be removed in the future.

## Converting a network graph file to the Shadow 2.0 format

The following will create a new network graph file `my-shadow-topology.gml`.

```bash
shadow/src/tools/convert_legacy_topology.py my-shadow-topology.graphml.xml > my-shadow-topology.gml
```
