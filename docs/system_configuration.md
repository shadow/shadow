# System Configs and Limits

Some Linux system configuration changes are needed to run large-scale Shadow
simulations (more than about 1000 processes). If you're just trying Shadow or
running small simulations, you can skip these steps.

## Number of Open Files

There is a default Linux system limit on the total number of open files. Since
Shadow opens files from within its own process space and not from within the
managed processes, both the system limit and the per-process limit must be
greater than the combined total number of files opened by all managed
processes. If each managed process in your simulation opens many files, you'll
likely want to increase the limit so that your application doesn't receive
`EMFILE` errors when calling `open()`.

### System-wide Limits

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
sudo sysctl -w fs.nr_open=10485760
echo "fs.nr_open = 10485760" | sudo tee -a /etc/sysctl.conf
sudo sysctl -w fs.file-max=10485760
echo "fs.file-max = 10485760" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

### User Limits

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

### systemd Limits

systemd may place a limit on the number of tasks that a user can run in its
slice. You can check to see if a limit is in place by running

```
$ systemctl status user-$UID.slice
```

Here's a listing of an example response:

```
● user-1027.slice - User Slice of <user>
   Loaded: loaded
Transient: yes
  Drop-In: /run/systemd/system/user-1027.slice.d
           └─50-After-systemd-logind\x2eservice.conf, 50-After-systemd-user-sessions\x2eservice.conf, 50-Description.conf, 50-TasksMax.conf
   Active: active since Wed 2020-05-06 21:20:08 EDT; 1 years 2 months ago
    Tasks: 81 (limit: 12288)
```

The last line of the listing shows that this user has a task limit of 12288
tasks.

If this task limit is too small, it can be removed with the following command:

```
$ sudo systemctl set-property user-$UID.slice TasksMax=infinity
```

## Number of Maps

There is a system limit on the number of `mmap()` mappings per process. Most
users will not have to modify these settings. However, if an application running
in Shadow makes extensive use of `mmap()`, you may need to increase the limit.

### Process Limit

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
echo "vm.max_map_count = 1073741824" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

## Process / Thread Count Limits

### System-Wide Limits

The kernel may limit the max-pid value to a small value, which will limit the
total number of possible processes running on the machine. This limit can be
raised by the command

```bash
sudo sysctl -w kernel.pid_max=4194304
echo "kernel.pid_max = 4194304" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

The kernel may also limit the total number of threads running on the machine.
This limit can be raised, too.

```bash
sudo sysctl -w kernel.threads-max=4194304
echo "kernel.threads-max = 4194304" | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

The kernel has a [fixed system-wide limit][linux-pid-limit] of 4,194,304
processes/threads. When running extremely large simulations, or when running
multiple simulations in parallel, you should be aware of this limit and ensure
the total number of processes/threads used by all simulations will not exceed
this limit.

The kernel may cap the `kernel.threads-max` value automatically so that, in the
maximum limit, the memory consumed by kernel thread control structures do not
consume more than approx. (1/8)th of system memory (see
<https://stackoverflow.com/a/21926745>).

[linux-pid-limit]: https://github.com/torvalds/linux/blob/b8481381d4e2549f06812eb6069198144696340c/include/linux/threads.h#L30-L35

### User Limits

You may need to raise the maximum number of user processes allowed in
`/etc/security/limits.conf`. For example, user limits can be removed with the
lines:

```
rjansen soft nproc unlimited
rjansen hard nproc unlimited
```

## For more information

<https://www.kernel.org/doc/Documentation/sysctl/fs.txt>  
<https://www.kernel.org/doc/Documentation/sysctl/vm.txt>

```bash
man proc
man ulimit
cat /proc/sys/fs/file-max
cat /proc/sys/fs/inode-max
```
