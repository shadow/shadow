# Shadow Simulator Tools

## Overview

This is a python package containing tools for working with the [shadow] simulator.

[shadow]: <https://shadow.github.io/>

It currently contains two modules:

* `shadowtools.config` - `TypedDict`s defining shadow's configuration file format.
  These are meant to facilitate dynamic generation of shadow config files
  from python code.

* `shadowtools.shadow_exec` - Streamlines running a single command in a
  single-host shadow simulation.

See the respective modules for further documentation and examples.

## Installation

This package isn't intended for publication on pypi since it is tightly coupled
with the simulator, which itself isn't officially packaged anywhere.

You can install it from a local checkout:

```
python3 -m pip install ./shadowtools
```

If you plan to make changes, you can add the ['--editable'][editable] pip
install flag to avoid the need to re-install after every modification.

[editable]: https://pip.pypa.io/en/stable/topics/local-project-installs/#editable-installs

Alternatively you install directly from the remote git repository:

```
python3 -m pip install git+https://github.com/shadow/shadow.git#subdirectory=shadowtools
```
