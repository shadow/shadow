## System Configs and Limits

Some Linux system configuration changes are needed to run large-scale Shadow
simulations (more than about 1000 nodes).

#### Number of Open Files

There is a default linux system limit on the number of open files. If each node
in your Shadow plug-in opens many file or socket descriptors (if you have many
nodes, this is very likely to happen), you'll likely want to increase the limit
so you application doesn't start getting errors when calling `open()` or
`socket()`.

###### System-wide Limits

Check the _system-wide_ limits with:

```bash
sysctl fs.nr_open # per-process open file limit
sysctl fs.file-max # system-wide open file limit
```

Use `cat /proc/sys/fs/file-nr` to find:
 1. the current, system-wide number of used file handles
 1. the current, system-wide number of free file handles
 1. and the system-wide limit on the maximum number of open files for all processes

Change the limits, persistent across reboots, and apply now:

```bash
sysctl -w fs.nr_open=10485760
echo "fs.nr_open = 10485760" >> /etc/sysctl.conf
sysctl -w fs.file-max=10485760
echo "fs.file-max = 10485760" >> /etc/sysctl.conf
sysctl -p
```

###### User Limits

Check the maximum number of open file descriptors _currently allowed_ in your
session:
```bash
ulimit -n
```

Check the number of files _currently used_ in a process with pid=PID:
```bash
/bin/ls -l /proc/PID/fd/ | wc -l
```

You will want to almost certainly want to raise the user file limit by modifying
`/etc/security/limits.conf`. For example:

```
rjansen soft nofile 10485760
rjansen hard nofile 10485760
```

The max you can use is your `fs.nr_open` system-wide limit setting from above.
You need to either log out and back in or reboot for the changes to take affect.
You can watch `/proc/sys/fs/file-nr` and reduce the limit according to your
usage, if you'd like.

#### Number of Maps

There is a system limit on the number of `mmap()` mappings per process. Most
users will not have to modify these settings. However, if an application running
in Shadow makes extensive use of `mmap()`, you may need to increase the limit.

###### Process Limit

The process limit can be queried in these ways:

```bash
sysctl vm.max_map_count
cat /proc/sys/vm/max_map_count
```

You can check the number of maps currently used in a process with pid=PID like
this:

```bash
wc -l /proc/PID/maps
```

Set a new limit, make it persistent, apply it now:

```bash
sudo sysctl -w vm.max_map_count=1073741824
sudo echo "vm.max_map_count = 1073741824" >> /etc/sysctl.conf
sudo sysctl -p
```

#### For more information

https://www.kernel.org/doc/Documentation/sysctl/fs.txt  
https://www.kernel.org/doc/Documentation/sysctl/vm.txt

```bash
man proc
man ulimit
cat /proc/sys/fs/file-max
cat /proc/sys/fs/inode-max
```
