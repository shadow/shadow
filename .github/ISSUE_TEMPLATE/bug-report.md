---
name: 'Bug report'
about: 'An error or flaw producing unexpected results'
title: ''
labels: 'Type: Bug'
assignees: ''

---

**Describe the issue**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behavior. If you problem involves a simulation, please include the configuration file that you used.

**Operating System (please complete the following information):**
 - OS and version: post the output of `lsb_release -d`  
[e.g., `CentOS Linux release 7.6.1810 (Core)`]
 - Kernel version: post the output of `uname -a`  
[e.g. `Linux sol 3.10.0-957.5.1.el7.x86_64 #1 SMP Fri Feb 1 14:54:57 UTC 2019 x86_64 x86_64 x86_64 GNU/Linux`]

**Shadow (please complete the following information):**
 - Version and build information: post the output of `shadow --show-build-info`
e.g.,
```
Shadow 3.1.0 — v3.1.0-67-g73bdff28c-dirty 2024-01-17--10:02:04
GLib 2.64.6
Built on 2024-01-26--04:37:06
Shadow was built with PROFILE=debug, OPT_LEVEL=1, [...]
```
 - Which processes you are trying to run inside the Shadow simulation:
[e.g. tor, tgen, nginx, python, etc.]

**Additional context**
Add any other context about the problem here. If submitting logs, enclose them in markdown code blocks using ` ``` ` characters.
