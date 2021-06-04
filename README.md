# The Shadow Simulator

[![Shadow Tests](https://github.com/shadow/shadow/actions/workflows/run_tests.yml/badge.svg?branch=dev&event=push)](https://github.com/shadow/shadow/actions/workflows/run_tests.yml?query=branch:dev+event:push)
[![Tor Tests](https://github.com/shadow/shadow/actions/workflows/run_tor.yml/badge.svg?branch=dev&event=push)](https://github.com/shadow/shadow/actions/workflows/run_tor.yml?query=branch:dev+event:push)

Shadow is a unique discrete-event network simulator that runs real 
applications like Tor and Bitcoin, and distributed systems of thousands of
nodes on a single machine. Shadow combines the accuracy of emulation with the 
efficiency and control of simulation, achieving the best of both approaches.

Quick Setup (installs everything in `~/.shadow`):
```
$ ./setup build --clean
$ ./setup test
$ ./setup install
```

Detailed Documentation
  + [docs/README.md](docs/README.md)

Questions:
  + https://github.com/shadow/shadow/discussions

Bug Reports:
  + https://github.com/shadow/shadow/issues

Shadow Project Development:
  + https://github.com/shadow
        
Homepage:
  + https://shadow.github.io
    
## Contributing

See [docs/contributing.md](docs/contributing.md)
