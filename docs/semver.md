# Stability Guarantees

Shadow generally follows the [semantic versioning](https://semver.org/)
principles:

- PATCH version increases (ex: `2.0.1` to `2.0.2`) are intended for bug fixes.
- MINOR version increases (ex: `2.0.2` to `2.1.0`) are intended for new
  backwards-compatible features and changes.
- MAJOR version increases (ex: `1.2.2` to `2.0.0`) are intended for
  incompatible changes.

More specifically, we aim to provide the following guarantees between MINOR
versions:

- Command line and configuration option changes and additions will be
  backwards-compatible.
	- Default values for existing options will not change.
- File and directory names in Shadow's data directory
  ([`general.data_directory`](shadow_config_spec.html#generaldata_directory))
  will not change.
	- If new files are added in Shadow's data directory, they will start with
	  the prefix `<hostname>.<process name>.<pid>`.
- The PID numbering (the values and their order) will not change.
- Any supported platforms will not be dropped.
	- A platform refers to an OS distribution's libraries and software, but
	  specifically not the kernel. Shadow may require a newer kernel version
      than is provided by the distribution.
- Any supported kernel versions will not be dropped.

The following may change between ANY versions (MAJOR, MINOR, or PATCH):

- The log format and messages may change.
- Experimental options may change or be removed.
- The simulation may produce different results.
