from __future__ import annotations

import json
import socket
import sys
import tempfile
import threading
import types
import unittest
import uuid
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

import lab02_canonical_run as canonical
from lab02_workflow_config import Lab02WorkflowConfig
from project_config import ProjectConfigError

ROOT = Path(__file__).resolve().parents[2]


class FakeSerial:
    instances = []
    marker = b"LAB02_READY build=dev fixture=inbox_64 mode=http\n"
    capture_log_path = None
    tail = b""
    pre_reset = b""
    reset_output = None
    fail_reset = False
    interrupt_reset = False
    straddle_read = False
    straddle_chunk = b""
    post_reset_chunk = b""
    read_count = 0
    straddle_started = None
    reset_released = None

    def __init__(self, port, baudrate, timeout):
        self.port = port
        self.dtr = None
        self._rts = False
        self.closed = False
        self.buffer = bytearray()
        self.buffer.extend(self.pre_reset)
        self.events = []
        self.__class__.instances.append(self)

    @property
    def rts(self):
        return self._rts

    @rts.setter
    def rts(self, value):
        self._rts = value
        self.events.append(("rts", value))
        if value:
            self.events.append(("log_exists", bool(self.capture_log_path and self.capture_log_path.is_file())))
            self.events.append(("read_entered", "read_entered" in self.events))
            self.events.append(("reader_owned", any(t.name.startswith("lab02-uart-") for t in threading.enumerate())))
            if self.straddle_read and self.straddle_started:
                if not self.straddle_started.wait(1):
                    raise AssertionError("reader did not enter straddling read before reset")
            if self.fail_reset:
                raise OSError("reset failed")
            if self.interrupt_reset:
                raise KeyboardInterrupt
        if not value and any(event == ("rts", True) for event in self.events):
            if not self.straddle_read:
                self.buffer.extend(self.marker if self.reset_output is None else self.reset_output)
            self.buffer.extend(self.tail)
            if self.reset_released:
                self.reset_released.set()

    def reset_input_buffer(self):
        self.events.append(("clear", None))
        self.buffer.clear()
        self.buffer.extend(self.pre_reset)

    def read(self, size):
        self.events.append(("read_entered",))
        self.__class__.read_count += 1
        read_count = self.__class__.read_count
        if self.straddle_read and read_count == 2:
            self.straddle_started.set()
            self.reset_released.wait(1)
            chunk = self.straddle_chunk
            if chunk:
                return chunk
            return b""
        if self.straddle_read and read_count >= 3 and self.post_reset_chunk:
            chunk = self.post_reset_chunk
            self.__class__.post_reset_chunk = b""
            return chunk
        if not self.buffer:
            import time
            time.sleep(0.01)
            return b""
        size = min(size, 11)
        chunk = bytes(self.buffer[:size])
        del self.buffer[:size]
        return chunk

    def close(self):
        self.closed = True


class FakeConfig:
    def path_value(self, name):
        if name == "firmware.logs":
            return ROOT / "logs/device"
        if name == "labs.registry":
            return ROOT / "config/labs.registry.yaml"
        raise AssertionError(name)


class CanonicalRunTests(unittest.TestCase):
    def _local_run(self, run_id, *, mode="manual", variant="recycle", seconds=1, tail=b"", marker_factory=None,
                   pre_reset=b"", reset_output=None, fail_reset=False, interrupt_reset=False,
                   straddle_chunk=None, post_reset_chunk=None):
        import shutil
        fake_module = types.ModuleType("serial")
        fake_module.Serial = FakeSerial
        FakeSerial.instances.clear()
        FakeSerial.tail = tail
        FakeSerial.pre_reset = pre_reset
        FakeSerial.reset_output = reset_output
        FakeSerial.fail_reset = fail_reset
        FakeSerial.interrupt_reset = interrupt_reset
        FakeSerial.straddle_read = straddle_chunk is not None or post_reset_chunk is not None
        FakeSerial.straddle_chunk = straddle_chunk or b""
        FakeSerial.post_reset_chunk = post_reset_chunk or b""
        FakeSerial.read_count = 0
        FakeSerial.straddle_started = threading.Event()
        FakeSerial.reset_released = threading.Event()
        selected = SimpleNamespace(kit_id="testkit", device="/dev/fake", kit=object(),
            deployment=SimpleNamespace(secrets_profile_for_kit=lambda _kit: None))
        build_dir = ROOT / ("build/labs/lab-02-bounded-inbox" if variant == "recycle" else
                            "build/labs/lab-02-bounded-inbox-scripted")
        build_dir.mkdir(parents=True, exist_ok=True)
        for name, data in (("lab02_bounded_inbox.bin", b"test-app"), ("flasher_args.json", b"{}"),
                           ("sdkconfig", b"CONFIG_TEST=y\n")):
            (build_dir / name).write_bytes(data)
        def build(*args, **kwargs):
            FakeSerial.marker = marker_factory(kwargs["frozen_build_id"]) if marker_factory else (
                f"LAB02_READY build={kwargs['frozen_build_id']} fixture=inbox_64 mode=recycle "
                f"harness={'1' if mode == 'scripted' else '0'} scenario=nominal\n").encode()
            if FakeSerial.straddle_chunk:
                FakeSerial.straddle_chunk = FakeSerial.straddle_chunk.replace(b"placeholder", kwargs["frozen_build_id"].encode())
            if FakeSerial.post_reset_chunk:
                FakeSerial.post_reset_chunk = FakeSerial.post_reset_chunk.replace(b"placeholder", kwargs["frozen_build_id"].encode())
            return build_dir
        try:
            with patch.dict(sys.modules, {"serial": fake_module}):
                return canonical.run_canonical(FakeConfig(), mode=mode, backend="local",
                    build_variant=variant, seconds=seconds, run_id=run_id,
                    target_resolver=lambda *a: selected, target_revalidator=lambda *a: selected.device,
                    build=build, flash_build=lambda *a: None,
                    parse=lambda _serial, metrics: metrics.write_text("{}\n"))
        finally:
            FakeSerial.tail = b""
            FakeSerial.pre_reset = b""
            FakeSerial.reset_output = None
            FakeSerial.fail_reset = False
            FakeSerial.interrupt_reset = False
            FakeSerial.straddle_read = False
            FakeSerial.straddle_chunk = b""
            FakeSerial.post_reset_chunk = b""
            for name in ("lab02_bounded_inbox.bin", "flasher_args.json", "sdkconfig"):
                (build_dir / name).unlink(missing_ok=True)

    def test_serial_redactor_handles_split_secret_and_event_values(self):
        redactor = canonical._SerialRedactor(["private-password"])
        out = redactor.feed(b"boot private-") + redactor.feed(b"password tail", final=True)
        self.assertEqual(out, b"boot [REDACTED] tail")
        safe = canonical._redact_event({"kind": "trace", "message": "private-password"}, ["private-password"])
        self.assertEqual(safe["message"], "[REDACTED]")

    def test_previous_run_build_identity_cannot_pass_as_fresh_boot(self):
        import shutil
        run_id = "w7-stale-" + uuid.uuid4().hex[:8]
        FakeSerial.marker = b"LAB02_READY build=stale fixture=inbox_64 mode=recycle harness=0 scenario=nominal\n"
        FakeSerial.tail = b""
        result = self._local_run(run_id, marker_factory=lambda _identity: FakeSerial.marker)
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        self.assertEqual(result, 1)
        self.assertEqual(data["terminal_reason"], "fresh_boot_identity_mismatch")
        shutil.rmtree(manifest.parent, ignore_errors=True)

    def test_unterminated_uart_line_is_bounded_and_preserved_as_partial_evidence(self):
        import shutil
        run_id = "w7-linecap-" + uuid.uuid4().hex[:8]
        result = self._local_run(run_id, tail=b"x" * (canonical.MAX_SERIAL_LINE_BYTES + 100))
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        serial_path = ROOT / "logs/device/labs/lab-02" / run_id / "serial.txt"
        self.assertEqual(result, 1)
        self.assertEqual(data["terminal_reason"], "serial_line_limit_exceeded")
        self.assertGreater(serial_path.stat().st_size, canonical.MAX_SERIAL_LINE_BYTES)
        shutil.rmtree(manifest.parent, ignore_errors=True)
        shutil.rmtree(serial_path.parent, ignore_errors=True)

    def test_scripted_completion_requires_expected_build_and_step_count(self):
        import shutil
        run_id = "w7-harness-" + uuid.uuid4().hex[:8]
        def wrong_done(identity):
            return (f"LAB02_READY build={identity} fixture=inbox_64 mode=recycle harness=1 scenario=nominal\n"
                    "HARNESS_DONE build=foreign steps=40 scenario=nominal\n").encode()
        result = self._local_run(run_id, mode="scripted", variant="scripted", marker_factory=wrong_done)
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        self.assertEqual(result, 1)
        self.assertEqual(data["terminal_reason"], "harness_failed")
        shutil.rmtree(manifest.parent, ignore_errors=True)

    def test_managed_http_runs_server_build_flash_capture_reset_and_manifest(self):
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            server_script = tmp_path / "server.py"
            server_script.write_text((ROOT / "server/labs/lab-02-paging/server.py").read_text())
            cfg = Lab02WorkflowConfig(server_script, "127.0.0.1", port, 64, "nominal",
                                      {key: value for key, value in {"startup": 5, "health": 5,
                                       "seed": 5, "readiness": 2, "action": 2, "cleanup": 2}.items()},
                                      f"http://127.0.0.1:{port}")
            build_dir = ROOT / "build/labs/lab-02-bounded-inbox-phaseb"
            build_dir.mkdir(parents=True, exist_ok=True)
            (build_dir / "lab02_bounded_inbox.bin").write_bytes(b"test-app")
            (build_dir / "flasher_args.json").write_text("{}")
            (build_dir / "sdkconfig").write_text("CONFIG_LAB02_FIXTURE_64=y\n")
            fake_module = types.ModuleType("serial")
            fake_module.Serial = FakeSerial
            FakeSerial.instances.clear()
            run_id = "w7-test-" + uuid.uuid4().hex[:8]
            flash_order = []
            profile = SimpleNamespace(wifi_ssid="private-ssid", wifi_password="private-password")
            selected = SimpleNamespace(kit_id="testkit", device="/dev/fake", kit=object(),
                deployment=SimpleNamespace(secrets_profile_for_kit=lambda _kit: profile))
            FakeSerial.marker = b"LAB02_READY build=dev fixture=inbox_64 mode=http private-password private-ssid\n"

            def build(*args, **kwargs):
                flash_order.append("build")
                FakeSerial.marker = (f"LAB02_READY build={kwargs['frozen_build_id']} fixture=inbox_64 "
                    "mode=http harness=0 scenario=nominal\n").encode()
                return build_dir

            def flash(*args, **kwargs):
                flash_order.append("flash")
                self.assertFalse(FakeSerial.instances)

            def parse(serial_path, metrics_path):
                metrics_path.write_text("{}\n")

            try:
                with patch.object(canonical, "load_lab02_workflow_config", return_value=cfg), \
                     patch.dict(sys.modules, {"serial": fake_module}):
                    FakeSerial.capture_log_path = ROOT / "logs/device/labs/lab-02" / run_id / "serial.txt"
                    result = canonical.run_canonical(FakeConfig(), mode="manual", kit="testkit",
                        seconds=1, run_id=run_id, target_resolver=lambda *a: selected,
                        target_revalidator=lambda *a: selected.device, build=build,
                        flash_build=flash, parse=parse)
                self.assertEqual(result, 0)
                self.assertEqual(flash_order, ["build", "flash"])
                serial = FakeSerial.instances[0]
                self.assertLess(serial.events.index(("clear", None)), serial.events.index(("rts", True)))
                self.assertLess(serial.events.index(("read_entered",)), serial.events.index(("rts", True)))
                self.assertIn(("log_exists", True), serial.events)
                self.assertIn(("reader_owned", True), serial.events)
                self.assertTrue(serial.closed)
                manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
                data = json.loads(manifest.read_text())
                self.assertEqual(data["workflow_status"], "PASS")
                self.assertEqual(data["experiment_verdict"], "UNMEASURED")
                self.assertEqual(data["firmware"]["build_id"], FakeSerial.marker.decode().split("build=", 1)[1].split()[0])
                self.assertEqual(data["fixture"]["sha256"], canonical.select_fixture(64).sha256)
                serial_text = (ROOT / "logs/device/labs/lab-02" / run_id / "serial.txt").read_text()
                self.assertNotIn("private-password", serial_text)
                self.assertNotIn("private-ssid", serial_text)
                self.assertNotIn("private-password", manifest.read_text())
                self.assertEqual(sum(1 for row in (manifest.parent / "events.jsonl").read_text().splitlines()
                                     if '"kind":"run_terminal"' in row), 1)
            finally:
                import shutil
                shutil.rmtree(ROOT / "docs/labs/evidence/lab-02/runs" / run_id, ignore_errors=True)
                shutil.rmtree(ROOT / "logs/device/labs/lab-02" / run_id, ignore_errors=True)
            for name in ("lab02_bounded_inbox.bin", "flasher_args.json", "sdkconfig"):
                (build_dir / name).unlink(missing_ok=True)
            FakeSerial.marker = b"LAB02_READY build=dev fixture=inbox_64 mode=http\n"

    def test_pre_reset_marker_is_logged_but_cannot_satisfy_fresh_readiness(self):
        import shutil
        run_id = "w7-prereset-" + uuid.uuid4().hex[:8]
        stale = b"LAB02_READY build=stale fixture=inbox_64 mode=recycle harness=0 scenario=nominal\n"
        result = self._local_run(run_id, marker_factory=lambda _identity: stale,
                                 pre_reset=stale, reset_output=b"")
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        serial_path = ROOT / "logs/device/labs/lab-02" / run_id / "serial.txt"
        self.assertEqual(result, 1)
        self.assertEqual(data["terminal_reason"], "fresh_boot_marker_timeout")
        self.assertIn(stale.decode().strip(), serial_path.read_text())
        shutil.rmtree(manifest.parent, ignore_errors=True)
        shutil.rmtree(serial_path.parent, ignore_errors=True)

    def test_identity_matching_marker_from_straddling_read_cannot_satisfy_freshness(self):
        import shutil
        run_id = "w7-readstart-straddle-" + uuid.uuid4().hex[:8]
        marker_factory = lambda identity: (f"LAB02_READY build={identity} fixture=inbox_64 mode=recycle "
            "harness=0 scenario=nominal\n").encode()
        result = self._local_run(run_id, marker_factory=marker_factory,
                                 straddle_chunk=marker_factory("placeholder"))
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        serial_path = ROOT / "logs/device/labs/lab-02" / run_id / "serial.txt"
        identity = data["firmware"]["build_id"]
        self.assertEqual(result, 1)
        self.assertEqual(data["workflow_status"], "INCOMPLETE")
        self.assertEqual(data["terminal_reason"], "fresh_boot_marker_timeout")
        self.assertIn(marker_factory(identity).decode().strip(), serial_path.read_text())
        shutil.rmtree(manifest.parent, ignore_errors=True)
        shutil.rmtree(serial_path.parent, ignore_errors=True)

    def test_read_started_after_reset_can_establish_fresh_readiness(self):
        import shutil
        run_id = "w7-readstart-fresh-" + uuid.uuid4().hex[:8]
        result = self._local_run(run_id, post_reset_chunk=(b"LAB02_READY build=placeholder fixture=inbox_64 "
                                  b"mode=recycle harness=0 scenario=nominal\n"))
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        self.assertEqual(result, 0)
        self.assertEqual(data["workflow_status"], "PASS")
        self.assertEqual(data["experiment_verdict"], "UNMEASURED")
        shutil.rmtree(manifest.parent, ignore_errors=True)
        shutil.rmtree(ROOT / "logs/device/labs/lab-02" / run_id, ignore_errors=True)

    def test_reset_exception_finalizes_and_closes_uart(self):
        import shutil
        run_id = "w7-resetfail-" + uuid.uuid4().hex[:8]
        result = self._local_run(run_id, fail_reset=True)
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        self.assertEqual(result, 1)
        self.assertEqual(data["terminal_reason"], "OSError")
        self.assertTrue(FakeSerial.instances[0].closed)
        shutil.rmtree(manifest.parent, ignore_errors=True)
        shutil.rmtree(ROOT / "logs/device/labs/lab-02" / run_id, ignore_errors=True)

    def test_interrupt_during_reset_finalizes_incomplete_and_closes_uart(self):
        import shutil
        run_id = "w7-resetinterrupt-" + uuid.uuid4().hex[:8]
        result = self._local_run(run_id, interrupt_reset=True)
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        self.assertEqual(result, 1)
        self.assertEqual(data["terminal_reason"], "interrupted")
        self.assertEqual(data["workflow_status"], "INCOMPLETE")
        self.assertTrue(FakeSerial.instances[0].closed)
        shutil.rmtree(manifest.parent, ignore_errors=True)
        shutil.rmtree(ROOT / "logs/device/labs/lab-02" / run_id, ignore_errors=True)

    def test_preflight_failure_writes_partial_manifest_once(self):
        run_id = "w7-prefail-" + uuid.uuid4().hex[:8]
        result = canonical.run_canonical(FakeConfig(), mode="manual", backend="local", run_id=run_id,
            target_resolver=lambda *a: (_ for _ in ()).throw(ProjectConfigError("invalid target")),
            target_revalidator=lambda *a: "", build=lambda *a, **k: None,
            flash_build=lambda *a: None, parse=lambda *a: None)
        self.assertEqual(result, 1)
        manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
        data = json.loads(manifest.read_text())
        self.assertEqual(data["workflow_status"], "FAIL")
        self.assertEqual(data["firmware"]["binary_sha256"], None)
        self.assertEqual(sum(1 for row in (manifest.parent / "events.jsonl").read_text().splitlines()
                             if '"kind":"run_terminal"' in row), 1)
        import shutil
        shutil.rmtree(manifest.parent, ignore_errors=True)

    def test_unsafe_configured_log_root_fails_before_target_or_flash(self):
        run_id = "w7-logroot-" + uuid.uuid4().hex[:8]
        with tempfile.TemporaryDirectory() as raw:
            config = FakeConfig()
            config.path_value = lambda name: Path(raw) if name == "firmware.logs" else ROOT / "config/labs.registry.yaml"
            result = canonical.run_canonical(config, mode="manual", backend="local", run_id=run_id,
                target_resolver=lambda *a: self.fail("target must not be resolved"),
                target_revalidator=lambda *a: "", build=lambda *a, **k: self.fail("must not build"),
                flash_build=lambda *a: self.fail("must not flash"), parse=lambda *a: None)
            manifest = ROOT / "docs/labs/evidence/lab-02/runs" / run_id / "run-manifest.json"
            data = json.loads(manifest.read_text())
            self.assertEqual(result, 1)
            self.assertEqual(data["terminal_reason"], "unsafe_artifact_root")
            import shutil
            shutil.rmtree(manifest.parent, ignore_errors=True)

    def test_local_and_eager_runs_share_manifest_capture_and_reset_path(self):
        fake_module = types.ModuleType("serial")
        fake_module.Serial = FakeSerial
        selected = SimpleNamespace(kit_id="testkit", device="/dev/fake", kit=object(),
            deployment=SimpleNamespace(secrets_profile_for_kit=lambda _kit: None))
        FakeSerial.instances.clear()
        FakeSerial.marker = b"LAB02_READY build=local fixture=inbox_64 mode=recycle harness=0 scenario=nominal\n"
        local_id = "w7-local-" + uuid.uuid4().hex[:8]
        local_build_dir = ROOT / "build/labs/lab-02-bounded-inbox"
        local_build_dir.mkdir(parents=True, exist_ok=True)
        (local_build_dir / "lab02_bounded_inbox.bin").write_bytes(b"test-local-app")
        (local_build_dir / "flasher_args.json").write_text("{}")
        (local_build_dir / "sdkconfig").write_text("CONFIG_LAB02_FIXTURE_64=y\n")
        try:
            with patch.dict(sys.modules, {"serial": fake_module}):
                def local_build(*args, **kwargs):
                    FakeSerial.marker = (f"LAB02_READY build={kwargs['frozen_build_id']} fixture=inbox_64 "
                        "mode=recycle harness=0 scenario=nominal\n").encode()
                    return local_build_dir
                result = canonical.run_canonical(FakeConfig(), mode="manual", backend="local",
                    build_variant="recycle", seconds=1, run_id=local_id,
                    target_resolver=lambda *a: selected, target_revalidator=lambda *a: selected.device,
                    build=local_build, flash_build=lambda *a: None,
                    parse=lambda serial_path, metrics_path: metrics_path.write_text("{}\n"))
            manifest = ROOT / "docs/labs/evidence/lab-02/runs" / local_id / "run-manifest.json"
            data = json.loads(manifest.read_text())
            self.assertEqual(result, 0)
            self.assertEqual(data["workflow_status"], "PASS")
            self.assertEqual(data["server"]["path"], "none")
            self.assertEqual(data["experiment_verdict"], "UNMEASURED")
        finally:
            import shutil
            shutil.rmtree(ROOT / "docs/labs/evidence/lab-02/runs" / local_id, ignore_errors=True)
            shutil.rmtree(ROOT / "logs/device/labs/lab-02" / local_id, ignore_errors=True)
            for name in ("lab02_bounded_inbox.bin", "flasher_args.json", "sdkconfig"):
                (local_build_dir / name).unlink(missing_ok=True)
            FakeSerial.marker = b"LAB02_READY build=dev fixture=inbox_64 mode=http\n"

        eager_id = "w7-eager-" + uuid.uuid4().hex[:8]
        eager_build_dir = ROOT / "build/labs/lab-02-bounded-inbox-eager"
        eager_build_dir.mkdir(parents=True, exist_ok=True)
        (eager_build_dir / "lab02_bounded_inbox.bin").write_bytes(b"test-eager-app")
        (eager_build_dir / "flasher_args.json").write_text("{}")
        (eager_build_dir / "sdkconfig").write_text("CONFIG_LAB02_EAGER_BASELINE=y\n")
        FakeSerial.marker = b"LAB02_READY build=eager fixture=inbox_64 mode=eager harness=0 scenario=nominal\n"
        FakeSerial.instances.clear()
        try:
            with patch.dict(sys.modules, {"serial": fake_module}):
                def eager_build(*args, **kwargs):
                    FakeSerial.marker = (f"LAB02_READY build={kwargs['frozen_build_id']} fixture=inbox_64 "
                        "mode=eager harness=0 scenario=nominal\n").encode()
                    return eager_build_dir
                result = canonical.run_canonical(FakeConfig(), mode="manual", backend="local",
                    build_variant="eager", seconds=1, run_id=eager_id,
                    target_resolver=lambda *a: selected, target_revalidator=lambda *a: selected.device,
                    build=eager_build, flash_build=lambda *a: None,
                    parse=lambda serial_path, metrics_path: metrics_path.write_text("{}\n"))
            manifest = ROOT / "docs/labs/evidence/lab-02/runs" / eager_id / "run-manifest.json"
            data = json.loads(manifest.read_text())
            self.assertEqual(result, 0)
            self.assertEqual(data["workflow_status"], "PASS")
            self.assertEqual(data["server"]["path"], "none")
            self.assertEqual(data["firmware"]["cmake_options"]["CONFIG_LAB_AUTO_HARNESS"], "n")
        finally:
            import shutil
            shutil.rmtree(ROOT / "docs/labs/evidence/lab-02/runs" / eager_id, ignore_errors=True)
            shutil.rmtree(ROOT / "logs/device/labs/lab-02" / eager_id, ignore_errors=True)
            for name in ("lab02_bounded_inbox.bin", "flasher_args.json", "sdkconfig"):
                (eager_build_dir / name).unlink(missing_ok=True)
            FakeSerial.marker = b"LAB02_READY build=dev fixture=inbox_64 mode=http\n"


if __name__ == "__main__":
    unittest.main()
