"""
CLI tool for running simple shadow simulations.

Can be executed as `shadow-exec` after installing the package, or without
installing e.g. as
`PYTHONPATH=/reporoot/shadowtools/src python3 -m shadowtools.shadow_exec`.

Examples:

```
$ shadow-exec date
Sat Jan  1 00:00:00 GMT 2000
```

```
$ shadow-exec -- bash -c 'date; sleep 1000; date'
Sat Jan  1 00:00:00 GMT 2000
Sat Jan  1 00:16:40 GMT 2000
```
"""

import argparse
import glob
import re
import enum
import os
import subprocess
import shlex
import shutil
import sys
import tempfile
import textwrap
import yaml

from pathlib import Path
from typing import TextIO, BinaryIO, Final, Optional, List, Iterable

import shadowtools.config as scfg


class PreserveChoice(enum.Enum):
    ALWAYS = enum.auto()
    NEVER = enum.auto()
    ON_ERROR = enum.auto()


def _glob_with_root_dir(pattern: str, root_dir: Path) -> List[str]:
    """Wrapper around glob.glob, backporting root_dir param.

    glob.glob added the root_dir parameter in python 3.10. When we stop
    supporting python 3.10 we can replace calls to this function with
    `glob.glob(pattern, root_dir=root_dir)`.
    """
    prefix = str(root_dir).removesuffix(os.sep) + os.sep
    rooted_pattern = prefix + pattern
    return [s.removeprefix(prefix) for s in glob.glob(rooted_pattern)]


def _try_open_glob(root_dir: Path, pattern: str) -> Optional[BinaryIO]:
    """Open and return a file matching `pattern` in `root_dir`, if one exists.

    Raises an exception if there are multiple matches."""
    matches = _glob_with_root_dir(pattern, root_dir)
    if len(matches) == 0:
        return None
    if len(matches) != 1:
        raise Exception(
            f"Unexpectedly more than 1 match at {root_dir}/{pattern}: {matches}"
        )
    return root_dir.joinpath(matches[0]).open("rb")


def _make_base_config() -> scfg.Config:
    """Generate a simple shadow configuration, suitable for one-off ad-hoc simulations.

    Does *not* include any hosts, which must be added separately.
    """
    return scfg.Config(
        general=scfg.General(
            # It'd be nice to set a higher stop-time here, but some simulations
            # (chutney) take a long time to fast-forward empty time after all
            # processes have exited.
            # TODO: investigate why this is and/or add a shadow feature to stop
            # early if all processes have exited.
            stop_time="100h",
            log_level="warning",
            heartbeat_interval=None,
            progress=False,
        ),
        experimental=scfg.Experimental(
            # For the sort of small simulations this tool is meant for, cpu
            # pinning is probably more trouble than its worth. e.g. multiple
            # simulations run at once will pin to the same cpu.
            use_cpu_pinning=False,
        ),
        network=scfg.Network(graph=scfg.Graph(type="1_gbit_switch")),
        hosts={},
    )


def _make_controller_host(args: Iterable[str]) -> scfg.Host:
    """Generate a shadow host configuration to run the command in `args`"""
    wrapper_script = textwrap.dedent(f"""
        set -euo pipefail

        # Change back to host working dir
        cd {shlex.quote(str(Path('.').resolve()))}

        # Run specified command
        exec {shlex.join(args)}
        """)
    return scfg.Host(
        network_node_id=0,
        processes=[
            scfg.Process(
                path="bash",
                args=[
                    "-c",
                    wrapper_script,
                ],
            )
        ],
    )


def _run_shadow_watching_process(
    *,
    watch_host: str,
    watch_pid: int,
    dstdir: Path,
    shadow_bin: Path = Path("shadow"),
    shadow_config_path: Path,
    shadow_args: Iterable[str] = (),
    stdout: BinaryIO = sys.stdout.buffer,
    stderr: BinaryIO = sys.stderr.buffer,
) -> None:
    """Run shadow, forwarding the stdout of one simulated process to `stdout`

    watch_host: Name of the host inside the shadow sim to watch.
    watch_pid: pid of the process on watch_host to watch.
    dstdir: directory in which to put output files.
    shadow_bin: path to the shadow executable.
    shadow_config_path: path to the shadow config file.
    shadow_args: additional command-line arguments to pass to shadow.
    stdout: where to write the watched process's stdout.
    stderr: where to write the watched process's stderr.
    """
    data_dir = dstdir.joinpath("shadow.data")
    if any((re.match(r"^--data-directory(=|$)|^-d", s) for s in shadow_args)):
        # It wouldn't be *terribly* hard to support this, but not today.
        # Naively allowing this override would break our stdout pass-through
        # below.
        raise Exception(
            f"ERROR: Overriding shadow's --data-directory currently unsupported.",
        )
    shadow_stdout_path = dstdir.joinpath("shadow.stdout")
    shadow_stderr_path = dstdir.joinpath("shadow.stderr")
    host_dir = data_dir.joinpath("hosts", watch_host)
    with shadow_stdout_path.open("w") as shadow_stdout_file, shadow_stderr_path.open(
        "w"
    ) as shadow_stderr_file:
        shadow_ps = subprocess.Popen(
            [str(shadow_bin)]
            + list(shadow_args)
            + [f"--data-directory={data_dir}", "--", str(shadow_config_path)],
            stdout=shadow_stdout_file,
            stderr=shadow_stderr_file,
        )
        simulated_stdout_file = None
        simulated_stderr_file = None
        shadow_exited = False
        while True:
            processed_data = False

            # Try opening the simulated process's stdout and stderr if we
            # haven't successfully done so yet.
            if simulated_stdout_file is None:
                simulated_stdout_file = _try_open_glob(
                    host_dir, f"*.{watch_pid}.stdout"
                )
            if simulated_stderr_file is None:
                simulated_stderr_file = _try_open_glob(
                    host_dir, f"*.{watch_pid}.stderr"
                )

            # Pump data from sim stdout and stderr to our stdout and stderr.
            # TODO: maybe this could be simplified using shutil.copyfileobj?
            for src, dest in [
                (simulated_stdout_file, stdout),
                (simulated_stderr_file, stderr),
            ]:
                data = None
                if src is not None:
                    # Fairly arbitrary, but impose *some* limit to avoid
                    # excessive buffering.
                    bufsize = 1_000_000
                    data = src.read(bufsize)
                if data:
                    processed_data = True
                    while data:
                        count = dest.write(data)
                        data = data[count:]
                else:
                    # Flush when there's currently no data available to read.
                    # Flushing makes output more responsive for interactive
                    # usage, but potentially intermingles stdout and stderr
                    # output, e.g. if they're both ultimately going to a
                    # console.  Only flushing when there's no more data tries to
                    # at least ensure we do it on reasonable boundaries. e.g. if
                    # the target program line-buffers its output, then this will
                    # *tend* to flush only at line boundaries.
                    dest.flush()

            if not processed_data and shadow_exited:
                # Done
                break

            if not processed_data:
                # No data ready to handle right now.

                try:
                    # Wait a bit for shadow to exit.
                    # We want this to be long enough to avoid burning CPU
                    # cycles, but short enough to keep latency of pumping data
                    # to stdout low.
                    timeout_secs = 1
                    shadow_ps.wait(timeout_secs)
                    # If we get here, then shadow exited.
                    # Mark as exited, but loop again in case more output has
                    # been written.
                    shadow_exited = True
                except subprocess.TimeoutExpired:
                    # shadow didn't exit. Loop to see if there's more data to
                    # process.
                    pass
    stdout.flush()
    stderr.flush()
    # if shadow failed, dump its stderr
    if shadow_ps.returncode:
        raise Exception(
            f"shadow exited with code {shadow_ps.returncode}. stderr:\n"
            + shadow_stderr_path.read_text()
        )


def _main(
    progname: str,
    args: Iterable[str],
    preserve: PreserveChoice = PreserveChoice.NEVER,
    temp_dir: Optional[Path] = None,
    stdout: TextIO = sys.stdout,
    stderr: TextIO = sys.stderr,
    shadow_bin: Path = Path("shadow"),
    shadow_args: Iterable[str] = (),
) -> None:
    """
    Run a program under shadow.

    args:
    progname -- String prefix to use for output originating from this function.
    args -- List of arguments of program to be run under shadow.
    preserve -- Whether to save the temporary directory containing the raw
                simulation config and results.
    stdout -- Destination for the simulated program's merged stdout and stderr.
    stderr -- Destination for other "meta" output.
    shadow_bin -- Shadow binary basename or path.
    """

    tmpdir = Path(tempfile.mkdtemp(prefix=f"{progname}-", dir=temp_dir))

    config = _make_base_config()
    hostname = "host"
    config["hosts"][hostname] = _make_controller_host(args)

    config_path = tmpdir.joinpath("shadow.yaml")
    config_path.write_text(yaml.safe_dump(config))

    # Flush before handing the underlying buffers over
    stdout.flush()
    stderr.flush()

    error = False
    try:
        _run_shadow_watching_process(
            watch_host=hostname,
            watch_pid=1000,
            dstdir=tmpdir,
            shadow_bin=shadow_bin,
            shadow_config_path=config_path,
            shadow_args=shadow_args,
            stdout=stdout.buffer,
            stderr=stderr.buffer,
        )
    except Exception:
        error = True
        raise
    finally:
        # Ensure all raw data written into the buffers are flushed
        stdout.flush()
        stderr.flush()

        # clean up temp files
        if preserve == PreserveChoice.ALWAYS or (
            preserve == PreserveChoice.ON_ERROR and error
        ):
            print(f"{progname}: Preserving tmpdir {tmpdir}", file=stderr)
        else:
            shutil.rmtree(tmpdir)


def __main__() -> None:
    """Raw main, suitable for use with `project.scripts` in `pyproject.toml`"""

    PROGNAME: Final[str] = "shadow-exec"

    parser = argparse.ArgumentParser(
        prog=PROGNAME,
        description=textwrap.dedent(f"""
            Executes the command `args` inside a single-host shadow simulation.

            Examples:
              {PROGNAME} date
              {PROGNAME} -- bash -c 'date; sleep 100; date'
            """),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-p",
        "--preserve",
        choices=["always", "never", "on-error"],
        default="never",
        help="Whether to preserve the raw simulation config and result",
    )
    parser.add_argument(
        "-t",
        "--temp-dir",
        default=None,
        type=Path,
        help=(
            "Override default root directory for temporary files."
            + " If specified, must already exist."
            + " A fresh directory will be created here,"
            + " and by default deleted. See --preserve."
        ),
    )
    parser.add_argument(
        "--shadow-bin",
        default=Path("shadow"),
        type=Path,
        help="shadow binary basename or path",
    )
    # We take a single shell-encoded string here and split it instead of taking
    # multiple strings, because otherwise argparse will try to interpret tokens
    # starting with - as a new option for itself.
    parser.add_argument(
        "-a",
        "--shadow-args",
        type=str,
        default="",
        help=("Shell-encoded list of arguments to pass through to shadow."),
    )
    parser.add_argument("args", nargs="+", help="command and arguments to execute")
    res = parser.parse_args()
    _main(
        progname=PROGNAME,
        args=res.args,
        # parser should have enforced a valid value here
        preserve=PreserveChoice[res.preserve.upper().translate({ord("-"): "_"})],
        shadow_bin=res.shadow_bin,
        temp_dir=res.temp_dir,
        shadow_args=shlex.split(res.shadow_args),
    )


if __name__ == "__main__":
    __main__()
