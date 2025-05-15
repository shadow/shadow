import os
import unittest
import subprocess

SHADOW_BIN = os.environ.get("SHADOW_BIN", "shadow")


class TestShadowExecCLI(unittest.TestCase):
    """
    We test shadow-exec through the command-line, which is currently its only
    stable interface, and exercises the command-line parsing.
    """

    def test_date_start_time(self) -> None:
        res = subprocess.check_output(
            [
                "python3",
                "-m",
                "shadowtools.shadow_exec",
                f"--shadow-bin={SHADOW_BIN}",
                "--preserve=on-error",
                "--shadow-args=--model-unblocked-syscall-latency=false",
                "--",
                "date",
                "-Ins",
            ],
            text=True,
        )
        self.assertEqual(res, "2000-01-01T00:00:00,000000000+00:00\n")

    def test_bash_script(self) -> None:
        res = subprocess.check_output(
            [
                "python3",
                "-m",
                "shadowtools.shadow_exec",
                f"--shadow-bin={SHADOW_BIN}",
                "--preserve=on-error",
                "--shadow-args=--model-unblocked-syscall-latency=false",
                "--",
                "bash",
                "-c",
                "date -Ins",
            ],
            text=True,
        )
        self.assertEqual(res, "2000-01-01T00:00:00,000000000+00:00\n")

    def test_bash_script_sleep(self) -> None:
        res = subprocess.check_output(
            [
                "python3",
                "-m",
                "shadowtools.shadow_exec",
                f"--shadow-bin={SHADOW_BIN}",
                "--preserve=on-error",
                "--shadow-args=--model-unblocked-syscall-latency=false",
                "--",
                "bash",
                "-c",
                "date -Ins; sleep 1.001; date -Ins",
            ],
            text=True,
        )
        self.assertEqual(
            res,
            "2000-01-01T00:00:00,000000000+00:00\n2000-01-01T00:00:01,001000000+00:00\n",
        )

    def test_extra_shadow_args(self) -> None:
        # Pass through a `--seed` argument and validate that it was used.
        res1 = subprocess.check_output(
            [
                "python3",
                "-m",
                "shadowtools.shadow_exec",
                f"--shadow-bin={SHADOW_BIN}",
                "--preserve=on-error",
                "--shadow-args=--seed 2",
                "--",
                "bash",
                "-c",
                "set -euo pipefail; head -c 8 /dev/urandom | base64",
            ],
            text=True,
        )
        res2 = subprocess.check_output(
            [
                "python3",
                "-m",
                "shadowtools.shadow_exec",
                f"--shadow-bin={SHADOW_BIN}",
                "--preserve=on-error",
                "--shadow-args=--seed 3",
                "--",
                "bash",
                "-c",
                "set -euo pipefail; head -c 8 /dev/urandom | base64",
            ],
            text=True,
        )
        self.assertNotEqual(res1, res2)
