import json
import itertools
import os
import sys
import tempfile
import types
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "client/labs/lab-10-record-canvas/tools"))

from lab_adapter_lab10 import (  # noqa: E402
    ADAPTER_IDENTITY,
    ADAPTER_VERSION,
    Adapter,
    MAX_SERIAL_BYTES,
    SCRIPTED_COMPLETION_GRACE_SECONDS,
    SCRIPTED_WORKLOAD_IDENTITY,
    _scripted_start_line_valid,
    _scripted_completion_observed,
    _workload,
    _print_capture_active,
    _capture_serial_bounded,
)
from lab_adapter_registry import resolve_adapter  # noqa: E402
from lab_registry import get_lab  # noqa: E402
import parse_lab_trace  # noqa: E402


SCRIPTED_STARTED_MARKER = (
    b"LAB10_SCRIPTED_DRAWING status=started source=ui_task "
    b"hardware_touch=0 duration_cap_ms=180000 cadence_min_ms=36 "
    b"cadence_profile=scripted-cadence-36-36-36-40-40-v2 "
    b"cadence_max_ms=52 stroke_points=200 planned_strokes=18 "
    b"planned_palette_changes=17 required_color_mask=0x3e "
    b"pen_up_gap_ms=400 pause_ms=780 accepted_point_cap=4096\n"
)


def _scripted_completion_marker(
    accepted_points=3600, strokes=18, palette_changes=17, color_mask="0x3e"
):
    return (
        "LAB10_SCRIPTED_DRAWING status=complete source=ui_task "
        "hardware_touch=0 terminal=duration_cap duration_cap_ms=180000 "
        f"accepted_points={accepted_points} strokes={strokes} "
        f"palette_changes={palette_changes} accepted_color_mask={color_mask}\n"
    ).encode("ascii")


class _FakeSerial:
    def __init__(self, *_args, **_kwargs):
        self.device = _args[0]
        self.dtr = None
        self.rts = None
        self.closed = False

    def reset_input_buffer(self):
        pass

    def read(self, _size):
        return b""

    def close(self):
        self.closed = True


class _ReplaySerial(_FakeSerial):
    def __init__(self, payload, *_args, **_kwargs):
        super().__init__(*_args, **_kwargs)
        self._payload = payload
        self._read = False

    def read(self, _size):
        if self._read:
            return b""
        self._read = True
        return self._payload


class _FakeClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        value = self.now
        self.now += 0.25
        return value


class _ReadDrivenClock:
    def __init__(self):
        self.now = 0.0

    def monotonic(self):
        return self.now


class _ScheduledSerial(_FakeSerial):
    def __init__(self, events, *_args, **_kwargs):
        super().__init__(*_args, **_kwargs)
        self._events = list(events)
        self._now = 0.0

    def read(self, _size):
        self._now += 0.25
        if self._events and self._now >= self._events[0][0]:
            return self._events.pop(0)[1]
        return b""


class _TimedSequenceSerial(_FakeSerial):
    """Return each supplied serial chunk in its own bounded read turn."""

    def __init__(self, events, clock, *_args, **_kwargs):
        super().__init__(*_args, **_kwargs)
        self._events = list(events)
        self._clock = clock

    def read(self, _size):
        if self._events:
            self._clock.now, chunk = self._events.pop(0)
            return chunk
        self._clock.now += 0.25
        return b""


class Lab10AdapterTests(unittest.TestCase):
    def test_scripted_workload_uses_fixed_runner_identity_by_default(self):
        with patch.dict("os.environ", {}, clear=True):
            self.assertEqual(_workload("scripted"), SCRIPTED_WORKLOAD_IDENTITY)
            self.assertEqual(_workload("deterministic"), SCRIPTED_WORKLOAD_IDENTITY)

    def test_scripted_workload_ignores_synthetic_identity_override(self):
        with patch.dict(
            "os.environ",
            {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"},
            clear=True,
        ):
            self.assertEqual(_workload("scripted"), SCRIPTED_WORKLOAD_IDENTITY)

    def test_manual_workload_still_honors_workload_override(self):
        with patch.dict(
            "os.environ", {"LAB10_WORKLOAD": "manual-sparse-variable-speed"}, clear=True
        ):
            self.assertEqual(
                _workload("manual"), "manual-sparse-variable-speed"
            )

    def test_scripted_start_requires_exact_cadence_profile(self):
        self.assertTrue(_scripted_start_line_valid(SCRIPTED_STARTED_MARKER))
        self.assertFalse(
            _scripted_start_line_valid(
                SCRIPTED_STARTED_MARKER.replace(
                    b"cadence_profile=scripted-cadence-36-36-36-40-40-v2 ", b""
                )
            )
        )
        self.assertFalse(
            _scripted_start_line_valid(
                SCRIPTED_STARTED_MARKER.replace(
                    b"scripted-cadence-36-36-36-40-40-v2",
                    b"scripted-cadence-36-36-36-40-40-v1",
                )
            )
        )

    @staticmethod
    def _scripted_markers():
        return SCRIPTED_STARTED_MARKER, _scripted_completion_marker()

    def test_scripted_delayed_start_gets_full_duration_and_completion(self):
        clock = _FakeClock()
        started, complete = self._scripted_markers()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(5.0, started), (185.0, complete)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "delayed-start", 180, "scripted"
                )
            self.assertIn("status=passed", path.read_text(encoding="utf-8"))
            self.assertGreaterEqual(clock.now, 185.0)

    def test_scripted_completion_after_duration_ends_within_grace(self):
        clock = _FakeClock()
        started, complete = self._scripted_markers()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(1.0, started), (183.0, complete)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "late-complete", 180, "scripted"
                )
            self.assertIn("status=passed", path.read_text(encoding="utf-8"))
            # The adapter observes the full bounded grace window so a late
            # duplicate start marker cannot be hidden by an early return.
            self.assertGreaterEqual(clock.now, 186.0)
            self.assertLessEqual(clock.now, 186.5)

    def test_scripted_completion_inside_early_receipt_tolerance_is_accepted(self):
        clock = _FakeClock()
        started, complete = self._scripted_markers()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(1.0, started), (180.5, complete)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "near-boundary", 180, "scripted"
                )
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                path.read_text(encoding="utf-8"),
            )

    def test_scripted_completion_just_outside_early_receipt_tolerance_is_rejected(self):
        clock = _FakeClock()
        started, complete = self._scripted_markers()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(1.0, started), (180.0, complete)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "outside-tolerance", 180, "scripted"
                )
            output = path.read_text(encoding="utf-8")
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured",
                output,
            )
            self.assertNotIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                output,
            )

    def test_early_valid_completion_is_not_reused_after_duration(self):
        clock = _FakeClock()
        started, complete = self._scripted_markers()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(1.0, started), (2.0, complete)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "early-complete", 180, "scripted"
                )
            output = path.read_text(encoding="utf-8")
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured",
                output,
            )
            self.assertNotIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                output,
            )

    def test_scripted_missing_completion_expires_after_duration_and_grace(self):
        clock = _FakeClock()
        started, _complete = self._scripted_markers()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(1.0, started)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "missing-complete", 180, "scripted"
                )
            self.assertIn("status=not_configured", path.read_text(encoding="utf-8"))
            self.assertLessEqual(clock.now, 187.0)

    def test_scripted_startup_timeout_is_bounded_and_unconfigured(self):
        clock = _FakeClock()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial([], *args, **kwargs)
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "no-start", 180, "scripted"
                )
            self.assertIn("status=not_configured", path.read_text(encoding="utf-8"))
            self.assertLessEqual(clock.now, 61.0)

    def test_malformed_scripted_start_markers_do_not_start_capture(self):
        clock = _FakeClock()
        started, _complete = self._scripted_markers()
        malformed = started.replace(
            b" required_color_mask=0x3e", b" required_color_mask=0x3e junk"
        )
        duplicate = started.replace(
            b" required_color_mask=0x3e",
            b" required_color_mask=0x3e required_color_mask=0x3e",
        )
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(1.0, malformed), (2.0, duplicate)], *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "malformed-start", 180, "scripted"
                )
            output = path.read_text(encoding="utf-8")
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured",
                output,
            )
            self.assertNotIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                output,
            )
            self.assertLessEqual(clock.now, 61.0)

    def test_repeated_valid_scripted_start_markers_fail_closed(self):
        started, complete = self._scripted_markers()
        device_output = started + started + complete
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ReplaySerial(device_output, *args, **kwargs)
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=_FakeClock().monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "duplicate-start", 180, "scripted"
                )
            output = path.read_text(encoding="utf-8")
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured",
                output,
            )
            self.assertNotIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                output,
            )

    def test_point_cap_count_cannot_qualify_as_scripted_completion(self):
        marker = _scripted_completion_marker(accepted_points=4096).decode("ascii")
        with tempfile.TemporaryDirectory() as td:
            serial_path = Path(td) / "serial.txt"
            serial_path.write_text(marker, encoding="utf-8")
            self.assertFalse(_scripted_completion_observed(serial_path))

    def test_observed_target_count_is_accepted_as_scripted_completion(self):
        marker = _scripted_completion_marker(accepted_points=3886).decode("ascii")
        with tempfile.TemporaryDirectory() as td:
            serial_path = Path(td) / "serial.txt"
            serial_path.write_text(marker, encoding="utf-8")
            self.assertTrue(_scripted_completion_observed(serial_path))

    def test_inclusive_lower_bound_is_accepted_as_scripted_completion(self):
        marker = _scripted_completion_marker(accepted_points=3400).decode("ascii")
        with tempfile.TemporaryDirectory() as td:
            serial_path = Path(td) / "serial.txt"
            serial_path.write_text(marker, encoding="utf-8")
            self.assertTrue(_scripted_completion_observed(serial_path))

    def test_one_below_lower_bound_is_rejected_as_scripted_completion(self):
        marker = _scripted_completion_marker(accepted_points=3399).decode("ascii")
        with tempfile.TemporaryDirectory() as td:
            serial_path = Path(td) / "serial.txt"
            serial_path.write_text(marker, encoding="utf-8")
            self.assertFalse(_scripted_completion_observed(serial_path))

    def test_inclusive_upper_bound_is_accepted_as_scripted_completion(self):
        marker = _scripted_completion_marker(accepted_points=4095).decode("ascii")
        with tempfile.TemporaryDirectory() as td:
            serial_path = Path(td) / "serial.txt"
            serial_path.write_text(marker, encoding="utf-8")
            self.assertTrue(_scripted_completion_observed(serial_path))

    def test_white_only_or_out_of_range_color_mask_cannot_qualify(self):
        for color_mask in ("0x02", "-1", "0x7e"):
            with self.subTest(color_mask=color_mask), tempfile.TemporaryDirectory() as td:
                serial_path = Path(td) / "serial.txt"
                serial_path.write_bytes(_scripted_completion_marker(
                    strokes=18, palette_changes=17, color_mask=color_mask
                ))
                self.assertFalse(_scripted_completion_observed(serial_path))

    def test_registry_selects_fixed_adapter_and_both_modes(self):
        spec = get_lab("lab10")
        self.assertEqual(spec.adapter_key, ADAPTER_IDENTITY)
        self.assertEqual(set(spec.variants), {"manual", "scripted"})
        self.assertEqual(resolve_adapter(spec).__class__.__module__, "lab_adapter_lab10")
        self.assertTrue((ROOT / spec.trace_parser).is_file())
        manifest = json.loads(
            (ROOT / "client/labs/lab-10-record-canvas/manifest.lab.json").read_text(
                encoding="utf-8"
            )
        )
        identity = SCRIPTED_WORKLOAD_IDENTITY
        self.assertEqual(manifest["scripted_device_workload"]["identity"], identity)
        self.assertEqual(
            manifest["runner"]["resolved_commands"]["deterministic"],
            "make lab.lab10.scripted KIT=<kit>",
        )
        self.assertIn(identity, spec.run_instructions)
        self.assertNotIn("LAB10_WORKLOAD=synthetic", spec.run_instructions)
        readme = (ROOT / "client/labs/lab-10-record-canvas/README.md").read_text(
            encoding="utf-8"
        )
        self.assertIn(identity, readme)
        self.assertNotIn("LAB10_WORKLOAD=synthetic", readme)

    def test_operator_banner_covers_manual_and_scripted_capture_contract(self):
        target = SimpleNamespace(kit_id="box3-test")
        args = SimpleNamespace(backend="managed-http")
        with patch("builtins.print") as print_line:
            _print_capture_active(target, args, "manual", 17)
            _print_capture_active(target, args, "scripted", 19)
        output = "\n".join(call.args[0] for call in print_line.call_args_list)
        self.assertIn(
            "CAPTURE ACTIVE — command=make lab.lab10; backend=managed-http; "
            "mode=manual; kit=box3-test; remaining=17s",
            output,
        )
        self.assertIn(
            "CAPTURE ACTIVE — command=make lab.lab10.scripted; backend=managed-http; "
            "mode=scripted; kit=box3-test; remaining=19s",
            output,
        )
        self.assertIn("UI task draws 18 paced strokes with real palette selections", output)
        self.assertIn(
            "deterministic 36/36/36/40/40 ms cadence (within allowed 36–52 ms bounds)",
            output,
        )
        self.assertIn("no synthetic hardware-touch evidence", output)

    def test_bounded_capture_writes_runner_markers_and_closes(self):
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(Serial=_FakeSerial)
            clock = _FakeClock()
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ), patch.dict("os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "adapter-run", 1, "scripted"
                )
            text = path.read_text()
            self.assertIn("LAB10_RUN run_id=adapter-run mode=scripted", text)
            self.assertIn(
                f"workload={SCRIPTED_WORKLOAD_IDENTITY}", text
            )
            self.assertNotIn(
                "workload=synthetic-sparse-multicolor-v1", text
            )
            self.assertIn("LAB10_SCENARIO", text)
            self.assertIn("status=not_configured", text)
            self.assertLessEqual(path.stat().st_size, MAX_SERIAL_BYTES)

    def test_manual_capture_ends_early_on_valid_terminal_marker(self):
        clock = _FakeClock()
        device_output = (
            b"LAB10_CAPTURE_TERMINAL outcome=stopped terminal_event_count=1\r\n"
        )
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ReplaySerial(
                    device_output, *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"),
                    "manual-early-stop", 30, "manual",
                )

            self.assertIn(device_output.decode().strip(), path.read_text())
            self.assertLess(clock.now, 30)

    def test_scripted_duration_cap_waits_for_completion_in_later_serial_read(self):
        started, complete = self._scripted_markers()
        terminal = b"LAB10_CAPTURE_TERMINAL outcome=duration_cap terminal_event_count=1\n"
        clock = _ReadDrivenClock()
        serial_events = [
            (0.0, started),
            (9.7, terminal),
            (9.8, complete),
        ]
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _TimedSequenceSerial(
                    serial_events, clock, *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"),
                    "scripted-split-terminal-completion", 10, "scripted",
                )

            output = path.read_text(encoding="utf-8")
            self.assertIn(terminal.decode().strip(), output)
            self.assertIn(complete.decode().strip(), output)
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                output,
            )

    def test_scripted_duration_cap_without_completion_never_qualifies(self):
        started, _complete = self._scripted_markers()
        terminal = b"LAB10_CAPTURE_TERMINAL outcome=duration_cap terminal_event_count=1\n"
        clock = _ReadDrivenClock()
        serial_events = [(0.0, started), (9.7, terminal)]
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _TimedSequenceSerial(
                    serial_events, clock, *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"),
                    "scripted-duration-cap-without-completion", 10, "scripted",
                )

            output = path.read_text(encoding="utf-8")
            self.assertIn(terminal.decode().strip(), output)
            self.assertNotIn("LAB10_SCRIPTED_DRAWING status=complete", output)
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured",
                output,
            )
            self.assertGreaterEqual(
                clock.now, 10 + SCRIPTED_COMPLETION_GRACE_SECONDS
            )

    def test_malformed_or_nonterminal_marker_does_not_end_capture_early(self):
        clock = _FakeClock()
        device_output = (
            b"LAB10_CAPTURE_TERMINAL outcome=stopped terminal_event_count=0\n"
            b"LAB10_CAPTURE_TERMINAL outcome=stopped terminal_event_count=1 extra=1\n"
        )
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ReplaySerial(
                    device_output, *args, **kwargs
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"),
                    "manual-invalid-terminal", 1, "manual",
                )

            self.assertIn(device_output.decode().strip(), path.read_text())
            self.assertGreaterEqual(clock.now, 1)

    def test_scripted_completion_marker_closes_workflow_without_experiment_pass(self):
        started, complete = self._scripted_markers()
        device_output = (
            b"LAB10_READY build=device-build trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n"
        ) + started + complete
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            device_lines = device_output.splitlines(keepends=True)
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ScheduledSerial(
                    [(0.25, b"".join(device_lines[:2])), (1.5, device_lines[2])],
                    *args,
                    **kwargs,
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=_FakeClock().monotonic
            ), patch.dict("os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "scripted-run", 1, "scripted"
                )

            text = path.read_text(encoding="utf-8")
            self.assertIn("LAB10_SCRIPTED_DRAWING status=complete", text)
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                text,
            )
            self.assertNotIn(
                "LAB10_SCENARIO id=synthetic-sparse-multicolor-v1 status=passed", text
            )
            self.assertNotIn("status=not_configured", text)
            parsed = parse_lab_trace.parse_file(
                path, expected_run_id="scripted-run", expected_mode="scripted"
            )
            self.assertEqual(parsed["workflow_status"], "PASS")
            self.assertEqual(parsed["experiment_verdict"], "UNMEASURED")
            self.assertTrue(parsed["diagnostics"]["scripted_completion_observed"])

    def test_early_or_stopped_scripted_marker_cannot_close_workflow(self):
        started, _complete = self._scripted_markers()
        device_output = (
            b"LAB10_READY build=device-build trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n"
            b"LAB10_CAPTURE_TERMINAL outcome=stopped terminal_event_count=1\n"
        )
        device_output = device_output.replace(
            b"LAB10_CAPTURE_TERMINAL", started + b"LAB10_CAPTURE_TERMINAL"
        )
        clock = _FakeClock()
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ReplaySerial(device_output, *args, **kwargs)
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "scripted-stopped", 180, "scripted"
                )

            text = path.read_text(encoding="utf-8")
            self.assertLess(clock.now, 180)
            self.assertIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=cancelled",
                text,
            )
            self.assertNotIn(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed",
                text,
            )
            parsed = parse_lab_trace.parse_file(
                path, expected_run_id="scripted-stopped", expected_mode="scripted"
            )
            self.assertEqual(parsed["workflow_status"], "INCOMPLETE")
            self.assertFalse(
                parsed["diagnostics"]["scripted_completion_observed"]
            )

    def test_scripted_capture_without_completion_marker_remains_unconfigured(self):
        device_output = b"LAB10_READY build=device-build trace_schema=lab10-record-trace/v1 physical_ring_wipe=1\n"
        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(
                Serial=lambda *args, **kwargs: _ReplaySerial(device_output, *args, **kwargs)
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=_FakeClock().monotonic
            ):
                path = _capture_serial_bounded(
                    fake_host, SimpleNamespace(device="/dev/fake"), "scripted-incomplete", 1, "scripted"
                )

            text = path.read_text(encoding="utf-8")
            self.assertIn("status=not_configured", text)
            parsed = parse_lab_trace.parse_file(
                path, expected_run_id="scripted-incomplete", expected_mode="scripted"
            )
            self.assertEqual(parsed["workflow_status"], "INCOMPLETE")
            self.assertEqual(parsed["experiment_verdict"], "UNMEASURED")

    def test_keyboard_interrupt_preserves_cancellation_marker(self):
        class CancelSerial(_FakeSerial):
            def read(self, _size):
                raise KeyboardInterrupt

        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(Serial=CancelSerial)
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", return_value=0.0
            ):
                with self.assertRaises(KeyboardInterrupt):
                    _capture_serial_bounded(
                        fake_host, SimpleNamespace(device="/dev/fake"), "cancel-run", 1, "manual"
                    )
            text = (Path(td) / "logs/device/labs/lab-10/cancel-run/serial.txt").read_text()
            self.assertIn("LAB10_CANCEL requested=1", text)

    def test_capture_stops_before_serial_log_overflow(self):
        class FloodSerial(_FakeSerial):
            def read(self, _size):
                return b"x" * 4096

        with tempfile.TemporaryDirectory() as td:
            fake_host = SimpleNamespace(ROOT=Path(td))
            fake_serial = types.SimpleNamespace(Serial=FloodSerial)
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=itertools.repeat(0.0)
            ):
                with self.assertRaisesRegex(RuntimeError, "exceeded"):
                    _capture_serial_bounded(
                        fake_host, SimpleNamespace(device="/dev/fake"), "overflow-run", 1, "manual"
                    )
            path = Path(td) / "logs/device/labs/lab-10/overflow-run/serial.txt"
            self.assertLessEqual(path.stat().st_size, MAX_SERIAL_BYTES)

    def test_runner_creates_bounded_evidence_and_parser_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            target = SimpleNamespace(device="/dev/fake", kit_id="test-kit")
            spec = SimpleNamespace(
                lab_id="LAB-10",
                evidence_slug="lab-10",
                project_dir=Path("client/labs/lab-10-record-canvas"),
                target="esp32s3",
                make_name="lab10",
                variants={"manual": SimpleNamespace(build_dir=Path("build/manual"))},
                default_variant="manual",
                capture_seconds_default=1,
            )
            args = SimpleNamespace(
                mode="manual", variant="manual", seconds=1, run_id="runner-manual",
                kit=None, port=None, no_build=True,
            )
            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_args, **_kwargs: False,
                resolve_run_target=lambda *_args: target,
                resolve_variant=lambda *_args: spec.variants["manual"],
            )

            def capture(_host, _target, run_id, _seconds, _mode):
                path = root / "logs/device/labs/lab-10" / run_id / "serial.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    "LAB10_READY build=lab10-test trace_schema=lab10-record-trace/v1 "
                    "physical_ring_wipe=0\n"
                    "LAB10_RUN run_id=runner-manual mode=manual "
                    "workload=manual-no-stroke-audio-control\n",
                    encoding="utf-8",
                )
                return path

            with patch("lab_adapter_lab10._parser_module", return_value=parse_lab_trace), \
                 patch("lab_adapter_lab10._capture_serial_bounded", side_effect=capture):
                self.assertEqual(Adapter().run(host, None, spec, args, mode="manual"), 0)

            run_dir = root / "docs/labs/evidence/lab-10/runs/runner-manual"
            manifest = json.loads((run_dir / "run-manifest.json").read_text(encoding="utf-8"))
            metrics = json.loads((run_dir / "metrics.json").read_text(encoding="utf-8"))
            serial = root / "logs/device/labs/lab-10/runner-manual/serial.txt"
            self.assertEqual(manifest["workflow_status"], "PASS")
            self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")
            self.assertEqual(manifest["capture"]["stages"]["scenario"], "not_configured")
            self.assertEqual(manifest["artifacts"]["metrics"],
                             "docs/labs/evidence/lab-10/runs/runner-manual/metrics.json")
            self.assertEqual(metrics["document"], parse_lab_trace.PARSER_DOCUMENT)
            self.assertLessEqual(serial.stat().st_size, MAX_SERIAL_BYTES)
            self.assertLessEqual((run_dir / "metrics.json").stat().st_size,
                                 parse_lab_trace.MAX_OUTPUT_BYTES)

    def _run_build_path_case(self, *, no_build):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            target = SimpleNamespace(device="/dev/fake", kit_id="test-kit")
            project_dir = root / "client/labs/lab-10-record-canvas"
            project_dir.mkdir(parents=True)
            source = project_dir / "main/app_main.c"
            source.parent.mkdir()
            source.write_text("new firmware source", encoding="utf-8")
            build_dir = root / "build/manual"
            build_dir.mkdir(parents=True)
            flasher_args = build_dir / "flasher_args.json"
            flasher_args.write_text('{"generation":"old"}\n', encoding="utf-8")
            (build_dir / "lab10_record_canvas.bin").write_bytes(b"old firmware")
            old_timestamp_ns = min(
                flasher_args.stat().st_mtime_ns,
                (build_dir / "lab10_record_canvas.bin").stat().st_mtime_ns,
            )
            os.utime(source, ns=(old_timestamp_ns + 1_000_000_000,) * 2)
            self.assertGreater(source.stat().st_mtime_ns, flasher_args.stat().st_mtime_ns)

            spec = SimpleNamespace(
                lab_id="LAB-10",
                evidence_slug="lab-10",
                project_dir=Path("client/labs/lab-10-record-canvas"),
                target="esp32s3",
                make_name="lab10",
                variants={"manual": SimpleNamespace(build_dir=Path("build/manual"))},
                default_variant="manual",
                capture_seconds_default=1,
            )
            args = SimpleNamespace(
                mode="manual", variant="manual", seconds=1,
                run_id=f"build-path-no-build-{no_build}", kit=None, port=None,
                no_build=no_build,
            )
            build_calls = []
            flash_inputs = []

            def build_lab(config, make_name, variant, **kwargs):
                build_calls.append((config, make_name, variant, kwargs))
                (build_dir / "build-generation.txt").write_text(
                    "rebuilt-from-current-source", encoding="utf-8"
                )
                return build_dir

            def flash_lab_build(_config, flashed_dir, _port):
                generation = flashed_dir / "build-generation.txt"
                flash_inputs.append(
                    generation.read_text(encoding="utf-8") if generation.exists() else "old-build"
                )

            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_args, **_kwargs: True,
                resolve_run_target=lambda *_args: target,
                resolve_variant=lambda _spec, variant: spec.variants[variant],
                build_lab=build_lab,
                # Model the old helper behavior with existing flash metadata.
                ensure_lab_built=lambda *_args, **_kwargs: build_dir,
                revalidate_run_target=lambda _config, resolved: resolved.device,
                flash_lab_build=flash_lab_build,
            )

            def capture(_host, _target, run_id, _seconds, _mode):
                path = root / "logs/device/labs/lab-10" / run_id / "serial.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    "LAB10_READY build=lab10-test trace_schema=lab10-record-trace/v1 "
                    "physical_ring_wipe=0\n"
                    f"LAB10_RUN run_id={run_id} mode=manual "
                    "workload=manual-no-stroke-audio-control\n",
                    encoding="utf-8",
                )
                return path

            with patch("lab_adapter_lab10._parser_module", return_value=parse_lab_trace), \
                 patch("lab_adapter_lab10._capture_serial_bounded", side_effect=capture):
                exit_code = Adapter().run(host, None, spec, args, mode="manual")

            run_dir = root / "docs/labs/evidence/lab-10/runs" / args.run_id
            manifest = json.loads((run_dir / "run-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(exit_code, 0)
            self.assertEqual(manifest["workflow_status"], "PASS")
            self.assertEqual(manifest["capture"]["stages"]["build"], "passed")
            if no_build:
                self.assertEqual(build_calls, [])
                self.assertEqual(flash_inputs, ["old-build"])
            else:
                self.assertEqual(len(build_calls), 1)
                self.assertEqual(build_calls[0][:3], (None, "lab10", "manual"))
                self.assertEqual(
                    build_calls[0][3],
                    {"kit": "test-kit", "port": "/dev/fake", "target": target},
                )
                self.assertEqual(flash_inputs, ["rebuilt-from-current-source"])

    def test_normal_run_rebuilds_changed_source_with_existing_flasher_args(self):
        self._run_build_path_case(no_build=False)

    def test_no_build_uses_existing_build_without_rebuilding(self):
        self._run_build_path_case(no_build=True)

    def test_scripted_completion_identity_matches_serial_events_metrics_and_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            target = SimpleNamespace(device="/dev/fake", kit_id="test-kit")
            spec = SimpleNamespace(
                lab_id="LAB-10",
                evidence_slug="lab-10",
                project_dir=Path("client/labs/lab-10-record-canvas"),
                target="esp32s3",
                make_name="lab10",
                variants={"scripted": SimpleNamespace(build_dir=Path("build/scripted"))},
                default_variant="scripted",
                capture_seconds_default=1,
            )
            args = SimpleNamespace(
                mode="scripted", variant="scripted", seconds=1, run_id="runner-scripted",
                kit=None, port=None, no_build=True,
            )
            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_args, **_kwargs: False,
                resolve_run_target=lambda *_args: target,
                resolve_variant=lambda *_args: spec.variants["scripted"],
            )

            def capture(_host, _target, run_id, _seconds, _mode):
                path = root / "logs/device/labs/lab-10" / run_id / "serial.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    f"LAB10_RUN run_id=runner-scripted mode=scripted "
                    f"workload={SCRIPTED_WORKLOAD_IDENTITY}\n"
                    f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=started\n"
                    "LAB10_READY build=lab10-test trace_schema=lab10-record-trace/v1 "
                    "physical_ring_wipe=0\n"
                    + SCRIPTED_STARTED_MARKER.decode("ascii")
                    + _scripted_completion_marker(accepted_points=4095).decode("ascii")
                    + f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed\n",
                    encoding="utf-8",
                )
                return path

            with patch("lab_adapter_lab10._parser_module", return_value=parse_lab_trace), \
                 patch("lab_adapter_lab10._capture_serial_bounded", side_effect=capture), \
                 patch.dict("os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}):
                self.assertEqual(Adapter().run(host, None, spec, args, mode="scripted"), 0)

            run_dir = root / "docs/labs/evidence/lab-10/runs/runner-scripted"
            manifest = json.loads((run_dir / "run-manifest.json").read_text(encoding="utf-8"))
            metrics = json.loads((run_dir / "metrics.json").read_text(encoding="utf-8"))
            events = [
                json.loads(line)
                for line in (run_dir / "events.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            serial_text = (
                root / "logs/device/labs/lab-10/runner-scripted/serial.txt"
            ).read_text(encoding="utf-8")
            scripted_identity = SCRIPTED_WORKLOAD_IDENTITY
            self.assertIn(f"workload={scripted_identity}", serial_text)
            self.assertIn(f"LAB10_SCENARIO id={scripted_identity} status=passed", serial_text)
            self.assertEqual(manifest["scenarios"], [scripted_identity])
            self.assertEqual(metrics["markers"]["run"][0]["workload"], scripted_identity)
            self.assertEqual(
                [item["id"] for item in metrics["markers"]["scenarios"]],
                [scripted_identity, scripted_identity],
            )
            run_events = [item for item in events if item.get("marker") == "LAB10_RUN"]
            scenario_events = [
                item for item in events if item.get("marker") == "LAB10_SCENARIO"
            ]
            self.assertEqual(len(run_events), 1)
            self.assertEqual(run_events[0]["workload"], scripted_identity)
            self.assertEqual(len(scenario_events), 1)
            self.assertEqual(scenario_events[0]["id"], scripted_identity)
            self.assertEqual(scenario_events[0]["status"], "passed")
            self.assertEqual(manifest["workflow_status"], "PASS")
            self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")
            self.assertEqual(manifest["capture"]["stages"]["scenario"], "passed")

    def test_missing_readiness_keeps_completed_scenario_passed_but_workflow_incomplete(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            target = SimpleNamespace(device="/dev/fake", kit_id="test-kit")
            spec = SimpleNamespace(
                lab_id="LAB-10", evidence_slug="lab-10",
                project_dir=Path("client/labs/lab-10-record-canvas"), target="esp32s3",
                make_name="lab10",
                variants={"scripted": SimpleNamespace(build_dir=Path("build/scripted"))},
                default_variant="scripted", capture_seconds_default=180,
            )
            args = SimpleNamespace(
                mode="scripted", variant="scripted", seconds=180,
                run_id="runner-missing-ready", kit=None, port=None, no_build=True,
            )
            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_args, **_kwargs: False,
                resolve_run_target=lambda *_args: target,
                resolve_variant=lambda *_args: spec.variants["scripted"],
            )

            def capture(_host, _target, run_id, _seconds, _mode):
                path = root / "logs/device/labs/lab-10" / run_id / "serial.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    f"LAB10_RUN run_id=runner-missing-ready mode=scripted "
                    f"workload={SCRIPTED_WORKLOAD_IDENTITY} adapter_version=v1\n"
                    f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=started\n"
                    + SCRIPTED_STARTED_MARKER.decode("ascii")
                    + _scripted_completion_marker(accepted_points=3886).decode("ascii")
                    + f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=passed\n",
                    encoding="utf-8",
                )
                return path

            with patch("lab_adapter_lab10._parser_module", return_value=parse_lab_trace), \
                 patch("lab_adapter_lab10._capture_serial_bounded", side_effect=capture), \
                 patch.dict("os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}):
                self.assertEqual(Adapter().run(host, None, spec, args, mode="scripted"), 1)

            run_dir = root / "docs/labs/evidence/lab-10/runs/runner-missing-ready"
            events = [
                json.loads(line)
                for line in (run_dir / "events.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            scenario_events = [
                item for item in events
                if item.get("marker") == "LAB10_SCENARIO"
            ]
            self.assertEqual(len(scenario_events), 1)
            self.assertEqual(scenario_events[0].get("status"), "passed")
            manifest = json.loads((run_dir / "run-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["capture"]["stages"]["scenario"], "passed")
            self.assertEqual(manifest["workflow_status"], "INCOMPLETE")
            self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")

    def test_scripted_missing_completion_keeps_manifest_scenario_unconfigured(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            target = SimpleNamespace(device="/dev/fake", kit_id="test-kit")
            spec = SimpleNamespace(
                lab_id="LAB-10", evidence_slug="lab-10",
                project_dir=Path("client/labs/lab-10-record-canvas"), target="esp32s3",
                make_name="lab10",
                variants={"scripted": SimpleNamespace(build_dir=Path("build/scripted"))},
                default_variant="scripted", capture_seconds_default=1,
            )
            args = SimpleNamespace(
                mode="scripted", variant="scripted", seconds=1, run_id="runner-incomplete",
                kit=None, port=None, no_build=True,
            )
            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_args, **_kwargs: False,
                resolve_run_target=lambda *_args: target,
                resolve_variant=lambda *_args: spec.variants["scripted"],
            )

            def capture(_host, _target, run_id, _seconds, _mode):
                path = root / "logs/device/labs/lab-10" / run_id / "serial.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(
                    f"LAB10_RUN run_id=runner-incomplete mode=scripted "
                    f"workload={SCRIPTED_WORKLOAD_IDENTITY}\n"
                    f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=started\n"
                    "LAB10_READY build=lab10-test trace_schema=lab10-record-trace/v1 "
                    "physical_ring_wipe=0\n"
                    f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured\n",
                    encoding="utf-8",
                )
                return path

            with patch("lab_adapter_lab10._parser_module", return_value=parse_lab_trace), \
                 patch("lab_adapter_lab10._capture_serial_bounded", side_effect=capture), \
                 patch.dict("os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}):
                self.assertEqual(Adapter().run(host, None, spec, args, mode="scripted"), 1)

            run_dir = root / "docs/labs/evidence/lab-10/runs/runner-incomplete"
            manifest = json.loads((run_dir / "run-manifest.json").read_text(encoding="utf-8"))
            metrics = json.loads((run_dir / "metrics.json").read_text(encoding="utf-8"))
            events = [
                json.loads(line)
                for line in (run_dir / "events.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            scripted_identity = SCRIPTED_WORKLOAD_IDENTITY
            self.assertEqual(manifest["scenarios"], [scripted_identity])
            self.assertEqual(metrics["markers"]["run"][0]["workload"], scripted_identity)
            self.assertEqual(
                [item["id"] for item in metrics["markers"]["scenarios"]],
                [scripted_identity, scripted_identity],
            )
            run_events = [item for item in events if item.get("marker") == "LAB10_RUN"]
            scenario_events = [
                item for item in events if item.get("marker") == "LAB10_SCENARIO"
            ]
            self.assertEqual(len(run_events), 1)
            self.assertEqual(run_events[0]["workload"], scripted_identity)
            self.assertEqual(len(scenario_events), 1)
            self.assertEqual(scenario_events[0]["id"], scripted_identity)
            self.assertEqual(scenario_events[0]["status"], "not_configured")
            serial_text = (
                root / "logs/device/labs/lab-10/runner-incomplete/serial.txt"
            ).read_text(encoding="utf-8")
            self.assertIn(f"workload={scripted_identity}", serial_text)
            self.assertTrue(
                serial_text.rstrip().endswith(
                    f"LAB10_SCENARIO id={scripted_identity} status=not_configured"
                )
            )
            self.assertNotIn("synthetic-sparse-multicolor-v1", serial_text)
            self.assertEqual(manifest["workflow_status"], "INCOMPLETE")
            self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")
            self.assertEqual(manifest["capture"]["stages"]["scenario"], "not_configured")

    def test_early_completion_keeps_serial_event_and_manifest_unconfigured(self):
        clock = _FakeClock()
        started, complete = self._scripted_markers()
        ready = (
            b"LAB10_READY build=device-build trace_schema=lab10-record-trace/v1 "
            b"physical_ring_wipe=1\n"
        )
        with tempfile.TemporaryDirectory() as td:
            root = Path(td).resolve()
            target = SimpleNamespace(device="/dev/fake", kit_id="test-kit")
            spec = SimpleNamespace(
                lab_id="LAB-10", evidence_slug="lab-10",
                project_dir=Path("client/labs/lab-10-record-canvas"), target="esp32s3",
                make_name="lab10",
                variants={"scripted": SimpleNamespace(build_dir=Path("build/scripted"))},
                default_variant="scripted", capture_seconds_default=180,
            )
            args = SimpleNamespace(
                mode="scripted", variant="scripted", seconds=180,
                run_id="runner-early-completion", kit=None, port=None, no_build=True,
            )
            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_args, **_kwargs: False,
                resolve_run_target=lambda *_args: target,
                resolve_variant=lambda *_args: spec.variants["scripted"],
            )
            fake_serial = types.SimpleNamespace(
                Serial=lambda *serial_args, **serial_kwargs: _ScheduledSerial(
                    [(1.0, ready + started), (2.0, complete)],
                    *serial_args,
                    **serial_kwargs,
                )
            )
            with patch.dict(sys.modules, {"serial": fake_serial}), patch(
                "lab_adapter_lab10.time.monotonic", side_effect=clock.monotonic
            ), patch("lab_adapter_lab10._parser_module", return_value=parse_lab_trace), patch.dict(
                "os.environ", {"LAB10_WORKLOAD": "synthetic-sparse-multicolor-v1"}
            ):
                self.assertEqual(
                    Adapter().run(host, None, spec, args, mode="scripted"), 1
                )

            run_dir = root / "docs/labs/evidence/lab-10/runs/runner-early-completion"
            serial_text = (
                root / "logs/device/labs/lab-10/runner-early-completion/serial.txt"
            ).read_text(encoding="utf-8")
            self.assertTrue(serial_text.rstrip().endswith(
                f"LAB10_SCENARIO id={SCRIPTED_WORKLOAD_IDENTITY} status=not_configured"
            ))
            events = [
                json.loads(line)
                for line in (run_dir / "events.jsonl").read_text(encoding="utf-8").splitlines()
            ]
            scenario_events = [
                item for item in events
                if item.get("marker") == "LAB10_SCENARIO"
            ]
            self.assertEqual(len(scenario_events), 1)
            self.assertEqual(scenario_events[0].get("status"), "not_configured")
            self.assertFalse(any(item.get("status") == "passed" for item in scenario_events))
            manifest = json.loads((run_dir / "run-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["capture"]["stages"]["scenario"], "not_configured")
            self.assertEqual(manifest["workflow_status"], "INCOMPLETE")


if __name__ == "__main__":
    unittest.main()
