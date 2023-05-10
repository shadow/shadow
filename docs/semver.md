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
  ([`general.data_directory`](shadow_config_spec.md#generaldata_directory))
  will not change.
- Support for any of Shadow's [supported platforms](supported_platforms.md)
  will not be dropped, unless those platforms no longer receive free updates
  and support from the distribution's developer.
- We will not change the criteria for the minimum supported Linux kernel version
as documented in [supported platforms](supported_platforms.md). (Note though
that this still allows us to increase the minimum kernel version as a result of
dropping support for a platform, which we may do as noted above).

The following may change between ANY versions (MAJOR, MINOR, or PATCH):

- The log format and messages.
- Experimental options may change or be removed.
- The simulation may produce different results.
- New files may be added in Shadow's data directory
  ([`general.data_directory`](shadow_config_spec.md#generaldata_directory)).
	- If new files are added in Shadow's host-data directories, they will begin
	  with the prefix `<process name>.<pid>`.
