# Managing Complex Configurations

It is sometimes useful to generate shadow configuration files dynamically.
Since Shadow accepts configuration files in [YAML
1.2](https://yaml.org/spec/1.2.2/) format, there are many options
available; even more so since [JSON](https://en.wikipedia.org/wiki/JSON) is
also valid YAML 1.2.

## YAML templating

YAML itself has some features to help avoid repetition. When using these
features, it can be helpful to use shadow's `--show-config` flag to examine the
"flat" generated config.

An individual node can be made into an *anchor* (`&AnchorName x`), and
referenced via an *alias* (`*AnchorName`). For example, here we create
and use the anchors `Node`, `Fast`, `Slow`, `ClientPath`, and `ServerPath`:

```yaml
general:
  stop_time: 10s
network:
  graph:
    type: 1_gbit_switch
hosts:
  fast_client:
    network_node_id: &Node 0
    bandwidth_up: &Fast "100 Mbit"
    bandwidth_down: *Fast
    processes:
    - path: &ClientPath "/path/to/client"
    # ...
  slow_client:
    network_node_id: *Node
    bandwidth_up: &Slow "1 Mbit"
    bandwidth_down: *Slow
    processes:
    - path: *ClientPath
    # ...
  fast_server:
    network_node_id: *Node
    bandwidth_up: *Fast
    bandwidth_down: *Fast
    processes:
    - path: &ServerPath "/path/to/server"
    # ...
  slow_server:
    network_node_id: *Node
    bandwidth_up: *Slow
    bandwidth_down: *Slow
    processes:
    - path: *ServerPath
```

We can use [extension
fields](https://docs.docker.com/compose/compose-file/#extension) to move our
constants into one place:

```yaml
x-constants:
  - &Node 0
  - &Fast "100 Mbit"
  - &Slow "1 Mbit"
  - &ClientPath "/path/to/client"
  - &ServerPath "/path/to/server"
general:
  stop_time: 10s
network:
  graph:
    type: 1_gbit_switch
hosts:
  fast_client:
    network_node_id: *Node
    bandwidth_up: *Fast
    bandwidth_down: *Fast
    processes:
    - path: *ClientPath
  slow_client:
    network_node_id: *Node
    bandwidth_up: *Slow
    bandwidth_down: *Slow
    processes:
    - path: *ClientPath
  fast_server:
    network_node_id: *Node
    bandwidth_up: *Fast
    bandwidth_down: *Fast
    processes:
    - path: *ServerPath
  slow_server:
    network_node_id: *Node
    bandwidth_up: *Slow
    bandwidth_down: *Slow
    processes:
    - path: *ServerPath
```

We can also use [merge keys](https://yaml.org/type/merge.html) to make
extendable templates for fast and slow hosts:

```yaml
x-constants:
  - &Node 0
  - &Fast "100 Mbit"
  - &Slow "1 Mbit"
  - &ClientPath "/path/to/client"
  - &ServerPath "/path/to/server"
  - &FastHost
    network_node_id: *Node
    bandwidth_up: *Fast
    bandwidth_down: *Fast
  - &SlowHost
    network_node_id: *Node
    bandwidth_up: *Slow
    bandwidth_down: *Slow
general:
  stop_time: 10s
network:
  graph:
    type: 1_gbit_switch
hosts:
  fast_client:
    <<: *FastHost
    processes:
    - path: *ClientPath
  slow_client:
    <<: *SlowHost
    processes:
    - path: *ClientPath
  fast_server:
    <<: *FastHost
    processes:
    - path: *ServerPath
  slow_server:
    <<: *SlowHost
    processes:
    - path: *ServerPath
```

## Dynamic Generation

There are many tools and libraries for generating YAML and JSON. These can be helpful for
representing more complex relationships between parameter values.

Suppose we want to add a cleanup process to each host that runs one second
before the simulation ends. Since YAML doesn't support arithmetic, the
following *doesn't* work:

```yaml
x-constants:
  - &StopTimeSec 10
  - &CleanupProcess
    # This will evaluate to the invalid time string "10 - 1"; not "9"
    start_time: *StopTimeSec - 1
    ...
# ...
```

In such cases it may be helpful to write your configuration in a language that does support
more advanced features that can generate YAML or JSON.

### Python example

We can achieve the desired effect in Python like so:

```python
#!/usr/bin/env python3

Node = 0
StopTimeSec = 10
Fast = "100 Mbit"
Slow = "1 Mbit"
ClientPath = "/path/to/client"
ServerPath = "/path/to/server"
FastHost = {
  'network_node_id': Node,
  'bandwidth_up': Fast,
  'bandwidth_down': Fast,
}
SlowHost = {
  'network_node_id': Node,
  'bandwidth_up': Slow,
  'bandwidth_down': Slow,
}
CleanupProcess = {
  'start_time': f'{StopTimeSec - 1}s',
  'path': '/path/to/cleanup',
}
config = {
  'general': {
    'stop_time': '10s',
  },
  'network': {
    'graph': {
      'type': '1_gbit_switch'
    },
  },
  'hosts': {
    'fast_client': {
      **FastHost,
      'processes': [
        {'path': ClientPath},
        CleanupProcess,
      ],
    },
    'slow_client': {
      **SlowHost,
      'processes': [
        {'path': ClientPath},
        CleanupProcess,
      ],
    },
    'fast_server': {
      **FastHost,
      'processes': [
        {'path': ServerPath},
        CleanupProcess,
      ],
    },
    'slow_server': {
      **SlowHost,
      'processes': [
        {'path': ServerPath},
        CleanupProcess,
      ],
    },
  },
}

import yaml
print(yaml.safe_dump(config))
```

### Nix example

There are also languages that specialize in doing this kind of advanced configuration generation.
For example, using [NixOs's config language](https://nixos.org/manual/nix/stable/language/index.html):

```nix
let
  Node = 0;
  StopTimeSec = 10;
  Fast = "100 Mbit";
  Slow = "1 Mbit";
  ClientPath = "/path/to/client";
  ServerPath = "/path/to/server";
  FastHost = {
    network_node_id = Node;
    bandwidth_up = Fast;
    bandwidth_down = Fast;
  };
  SlowHost = {
    network_node_id = Node;
    bandwidth_up = Slow;
    bandwidth_down = Slow;
  };
  CleanupProcess = {
    start_time = (toString (StopTimeSec - 1)) + "s";
    path = "/path/to/cleanup";
  };
in
{
  general = {
    stop_time = (toString StopTimeSec) + "s";
  };
  network = {
    graph = {
      type = "1_gbit_switch";
    };
  };
  hosts = {
    fast_client = FastHost // {
      processes = [
        {path = ClientPath;}
        CleanupProcess
      ];
    };
    slow_client = SlowHost // {
      processes = [
        {path = ClientPath;}
        CleanupProcess
      ];
    };
    fast_server = FastHost // {
      processes = [
        {path = ServerPath;}
        CleanupProcess
      ];
    };
    slow_server = SlowHost // {
      processes = [
        {path = ServerPath;}
        CleanupProcess
      ];
    };
  };
}
```

This can be converted to JSON, which is also valid YAML, with:

```
nix eval -f example.nix --json
```