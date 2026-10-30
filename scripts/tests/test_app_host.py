import io
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from deployment_config import Deployment, DeploymentMeta, Endpoint, Kit
from device_targets import Port, connected_registered_kits, kit_nicknames
from app_host import prompt_kit_choice, resolve_flash_kit, stream_serial_lines
from project_config import ProjectConfigError


def roster():
    return Deployment(
        Path("unused"),
        "local",
        DeploymentMeta("desk", "Desk"),
        (),
        (Endpoint("e-a", "", "a", "desk"), Endpoint("e-b", "", "b", "desk")),
        (Kit("k-a", "e-a", usb_serial="AA:11"), Kit("k-b", "e-b", usb_serial="BB:22")),
        {"mazi": "e-a", "arlo": "e-b"},
    )


class AppHostKitTests(unittest.TestCase):
    def test_connected_registered_kits_filters_roster(self):
        deployment = roster()
        connected = connected_registered_kits(deployment, [Port("COM1", "AA11"), Port("COM2", "BB22")])
        self.assertEqual({kit.id for kit in connected}, {"k-a", "k-b"})

    def test_resolve_flash_kit_single_connected(self):
        deployment = roster()
        config = object()
        with patch("app_host.load_active_deployment", return_value=deployment), patch(
            "app_host.ports", return_value=[Port("COM1", "AA11")]
        ):
            self.assertEqual(resolve_flash_kit(config), "k-a")

    def test_resolve_flash_kit_multiple_non_tty_errors(self):
        deployment = roster()
        config = object()
        with patch("app_host.load_active_deployment", return_value=deployment), patch(
            "app_host.ports", return_value=[Port("COM1", "AA11"), Port("COM2", "BB22")]
        ), patch("app_host.sys.stdin") as stdin:
            stdin.isatty.return_value = False
            with self.assertRaisesRegex(ProjectConfigError, "set KIT="):
                resolve_flash_kit(config)

    def test_prompt_kit_choice_by_number(self):
        deployment = roster()
        with patch("builtins.input", return_value="2"):
            self.assertEqual(prompt_kit_choice(deployment, list(deployment.kits)), "k-b")

    def test_prompt_kit_choice_by_nickname(self):
        deployment = roster()
        with patch("builtins.input", return_value="mazi"):
            self.assertEqual(prompt_kit_choice(deployment, list(deployment.kits)), "k-a")

    def test_kit_nicknames_prefers_alias(self):
        deployment = roster()
        self.assertEqual(kit_nicknames(deployment, deployment.kits[0]), ["mazi"])


class FakeSerial:
    def __init__(self, chunks):
        self.chunks = list(chunks)

    def read(self, _size):
        if not self.chunks:
            raise KeyboardInterrupt
        return self.chunks.pop(0)


class AppSerialLogTests(unittest.TestCase):
    def test_lines_split_across_reads_are_timestamped_once(self):
        log, echo = io.StringIO(), io.StringIO()
        connection = FakeSerial([b"I (1) app: bu", b"ild=abc\r\nI (2) prod", b"uct_ui_ready\r\n"])
        with self.assertRaises(KeyboardInterrupt):
            stream_serial_lines(connection, log, echo, now=lambda: "T")
        self.assertEqual(log.getvalue(), "[T] I (1) app: build=abc\n[T] I (2) product_ui_ready\n")
        self.assertEqual(echo.getvalue(), log.getvalue())

    def test_duration_stops_and_keeps_partial_line(self):
        ticks = iter([0.0, 0.0, 0.5, 2.0])
        log, echo = io.StringIO(), io.StringIO()
        connection = FakeSerial([b"", b"partial"])
        written = stream_serial_lines(connection, log, echo, seconds=1.0, clock=lambda: next(ticks), now=lambda: "T")
        self.assertEqual(written, 1)
        self.assertEqual(log.getvalue(), "[T] partial\n")

    def test_invalid_utf8_does_not_stop_the_log(self):
        log, echo = io.StringIO(), io.StringIO()
        with self.assertRaises(KeyboardInterrupt):
            stream_serial_lines(FakeSerial([b"\xff\xfeok\n"]), log, echo, now=lambda: "T")
        self.assertTrue(log.getvalue().endswith("ok\n"))


class AppMakeAliasTests(unittest.TestCase):
    def test_make_server_runs_p1_wire(self):
        import os
        import subprocess

        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", "server"],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("server/device/main/server.py", result.stdout)
        self.assertIn("--port 8777", result.stdout)

    def test_app_runs_build_then_flash(self):
        import os
        import subprocess

        root = Path(__file__).resolve().parents[2]
        env = os.environ.copy()
        for name in ("KIT", "PORT"):
            env.pop(name, None)
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", "app"],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        build = result.stdout.index("scripts/app_host.py build")
        flash = result.stdout.index("scripts/app_host.py flash")
        self.assertLess(build, flash)

    def test_app_flash_forwards_kit_and_port(self):
        import os
        import subprocess

        root = Path(__file__).resolve().parents[2]
        env = os.environ.copy()
        for name in ("KIT", "PORT"):
            env.pop(name, None)
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", "app:flash", "KIT=alpha", "PORT=COM9"],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('scripts/app_host.py flash --kit "alpha" --port "COM9"', result.stdout)

    def test_app_monitor_forwards_port_and_filter(self):
        import os
        import subprocess

        root = Path(__file__).resolve().parents[2]
        env = os.environ.copy()
        for name in ("KIT", "PORT", "FILTER"):
            env.pop(name, None)
        result = subprocess.run(
            [
                "make",
                "--no-print-directory",
                "-n",
                "app:monitor",
                "PORT=/dev/cu.usbmodem101",
                "FILTER=product_mute:I",
            ],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("scripts/app_host.py monitor", result.stdout)
        self.assertIn('--port "/dev/cu.usbmodem101"', result.stdout)
        self.assertIn('--print-filter "product_mute:I"', result.stdout)

    def test_app_log_forwards_port_out_and_duration(self):
        import os
        import subprocess

        root = Path(__file__).resolve().parents[2]
        env = os.environ.copy()
        for name in ("KIT", "PORT", "OUT", "DURATION"):
            env.pop(name, None)
        result = subprocess.run(
            ["make", "--no-print-directory", "-n", "app:log", "PORT=COM7", "OUT=logs/x.log", "DURATION=30"],
            cwd=root,
            env=env,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("scripts/app_host.py log", result.stdout)
        self.assertIn('--port "COM7"', result.stdout)
        self.assertIn('--out "logs/x.log"', result.stdout)
        self.assertIn('--seconds "30"', result.stdout)


if __name__ == "__main__":
    unittest.main()
