"""
Types for constructing a shadow configuration file.

The types defined here are intended to facilitate construction of configuration
files for the shadow simulator. They are based on python TypedDicts, which are
just regular dictionaries at runtime, but allow us to document the expected
types, and validate with python type-checking tools such as mypy.

For the full and up-to-date specification of shadow's config file format, see
https://shadow.github.io/docs/guide/shadow_config_spec.html

Example to generate a shadow config and write to a file:

```
import pathlib
import shadowtools.config as scfg
import yaml

config = scfg.Config(
    general=scfg.General(
        stop_time="1h",
    ),
    network=scfg.Network(graph=scfg.Graph(type="1_gbit_switch")),
    hosts={
        "host": scfg.Host(
            network_node_id=0,
            processes=[
                scfg.Process(
                    path="echo",
                    args=[
                        "hello",
                        "world",
                    ],
                )
            ],
        )
    },
)
pathlib.Path("shadow.yml").write_text(yaml.safe_dump(config))
```
"""

from typing import TypedDict, Union, Literal, List, Dict

LogLevel = Union[
    Literal["error"],
    Literal["warning"],
    Literal["info"],
    Literal["debug"],
    Literal["trace"],
]
UnixSignal = Union[int, str]


class GraphFile(TypedDict, total=False):
    path: str
    compression: Union[Literal["xz"], None]


class Graph(TypedDict, total=False):
    type: Union[Literal["gml"], Literal["1_gbit_switch"]]
    file: GraphFile
    inline: str


class Network(TypedDict, total=False):
    use_shortest_path: bool
    graph: Graph


class General(TypedDict, total=False):
    bootstrap_end_time: Union[str, int]
    data_directory: str
    heartbeat_interval: Union[str, int, None]
    log_level: LogLevel
    model_unblocked_syscall_latency: bool
    parallelism: int
    progress: bool
    seed: int
    stop_time: Union[str, int]
    template_directory: Union[str, None]


class Experimental(TypedDict, total=False):
    interface_qdisc: Union[Literal["fifo"], Literal["round-robin"]]
    max_unapplied_cpu_latency: str
    report_errors_to_stderr: bool
    runahead: Union[str, None]
    scheduler: Union[Literal["thread-per-core"], Literal["thread-per-host"]]
    socket_recv_autotune: bool
    socket_recv_buffer: Union[str, int]
    socket_send_autotune: bool
    socket_send_buffer: Union[str, int]
    strace_logging_mode: Union[
        Literal["off"], Literal["standard"], Literal["deterministic"]
    ]
    unblocked_syscall_latency: str
    unblocked_vdso_latency: str
    use_cpu_pinning: bool
    use_dynamic_runahead: bool
    use_memory_manager: bool
    use_new_tcp: bool
    use_object_counters: bool
    use_preload_libc: bool
    use_preload_openssl_crypto: bool
    use_preload_openssl_rng: bool
    use_sched_fifo: bool
    use_syscall_counters: bool
    use_worker_spinning: bool


class HostOptions(TypedDict, total=False):
    log_level: Union[LogLevel, None]
    pcap_capture_size: Union[str, int]
    pcap_enabled: bool


class Exited(TypedDict):
    exited: int


class Signaled(TypedDict):
    signaled: UnixSignal


class Process(TypedDict, total=False):
    args: Union[str, List[str]]
    environment: Dict[str, str]
    expected_final_state: Union[Exited, Signaled, Literal["running"]]
    path: str
    shutdown_signal: UnixSignal
    shutdown_time: Union[str, int, None]
    start_time: Union[str, int]


class Host(TypedDict, total=False):
    bandwidth_down: Union[str, int, None]
    bandwidth_up: Union[str, int, None]
    ip_addr: Union[str, None]
    network_node_id: int
    host_options: HostOptions
    processes: List[Process]


class Config(TypedDict, total=False):
    general: General
    network: Network
    experimental: Experimental
    host_options_defaults: HostOptions
    hosts: Dict[str, Host]
