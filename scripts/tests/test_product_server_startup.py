"""Focused startup tests for product server dependency bootstrap."""
from __future__ import annotations

import builtins
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SERVER_PATH = ROOT / "server/device/main/server.py"
_SERVER_SPEC = importlib.util.spec_from_file_location(
    "product_server_startup_under_test", SERVER_PATH
)
server = importlib.util.module_from_spec(_SERVER_SPEC)
assert _SERVER_SPEC.loader is not None
sys.modules[_SERVER_SPEC.name] = server
_SERVER_SPEC.loader.exec_module(server)


class ProductServerStartupTests(unittest.TestCase):
    def _missing_runtime_import(self, name, *args, **kwargs):
        if name in {"yaml", "jsonschema"}:
            raise ImportError(f"missing test dependency: {name}")
        return self._real_import(name, *args, **kwargs)

    def setUp(self):
        self._real_import = builtins.__import__

    def test_host_reexecs_same_resolved_venv_target_and_preserves_argv(self):
        with tempfile.TemporaryDirectory(prefix="product-server-startup-") as temp:
            root = Path(temp)
            venv_bin = root / ".venv" / "bin"
            venv_bin.mkdir(parents=True)
            host_python = root / "host-python"
            host_python.write_bytes(b"host executable")
            candidate = venv_bin / "python"
            candidate.symlink_to(host_python)

            argv = ["server.py", "--run-id", "p7", "--port", "0"]
            with (
                mock.patch.object(server, "ROOT", root),
                mock.patch.object(server.sys, "executable", str(host_python)),
                mock.patch.object(server.sys, "prefix", str(root / "host")),
                mock.patch.object(server.sys, "base_prefix", str(root / "host")),
                mock.patch.object(server.sys, "argv", argv),
                mock.patch.object(builtins, "__import__", side_effect=self._missing_runtime_import),
                mock.patch.object(server.os, "execve") as execve,
            ):
                server._ensure_deployment_runtime()

            execve.assert_called_once()
            executable, child_argv, child_environment = execve.call_args.args
            self.assertEqual(executable, str(candidate))
            self.assertEqual(
                child_argv,
                [str(candidate), str(SERVER_PATH.resolve()), *argv[1:]],
            )
            self.assertEqual(
                child_environment[server._RUNTIME_REEXEC_ENV], str(candidate)
            )

    def test_reexec_marker_prevents_a_loop_if_venv_still_lacks_dependencies(self):
        with tempfile.TemporaryDirectory(prefix="product-server-startup-") as temp:
            root = Path(temp)
            candidate = root / ".venv" / "bin" / "python"
            candidate.parent.mkdir(parents=True)
            candidate.write_bytes(b"venv executable")
            environment = {server._RUNTIME_REEXEC_ENV: str(candidate)}

            with (
                mock.patch.object(server, "ROOT", root),
                mock.patch.dict(os.environ, environment, clear=False),
                mock.patch.object(builtins, "__import__", side_effect=self._missing_runtime_import),
                mock.patch.object(server.os, "execve") as execve,
            ):
                server._ensure_deployment_runtime()

            execve.assert_not_called()

    def test_active_repository_venv_fails_closed_without_reexec_loop(self):
        with tempfile.TemporaryDirectory(prefix="product-server-startup-") as temp:
            root = Path(temp)
            candidate = root / ".venv" / "bin" / "python"
            candidate.parent.mkdir(parents=True)
            candidate.write_bytes(b"venv executable")

            with (
                mock.patch.object(server, "ROOT", root),
                mock.patch.object(server.sys, "prefix", str(root / ".venv")),
                mock.patch.object(server.sys, "base_prefix", str(root / "host")),
                mock.patch.object(builtins, "__import__", side_effect=self._missing_runtime_import),
                mock.patch.object(server.os, "execve") as execve,
            ):
                server._ensure_deployment_runtime()

            execve.assert_not_called()


if __name__ == "__main__":
    unittest.main()
