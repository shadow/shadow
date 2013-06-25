**NOTE** - _This page is incomplete_

Generic applications may be run in Shadow. The most important required features of the application code to enable this are:
 + completely non-blocking I/O and non-blocking system calls
 + polling I/O events using the `epoll` interface (see `$ man epoll`)
 + no process forking or thread creation

The [shadow-plugins-extra repository](https://github.com/shadow/shadow-plugins-extra) contains a useful basic "hello world" example that illustrates how a program running outside of Shadow may also be run inside of Shadow. The example provides useful comments and a general structure that will be useful to understand when writing your own plug-ins.