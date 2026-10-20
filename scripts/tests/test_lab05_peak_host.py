"""LAB-05 peak host registration, parser, and injected-capture checks."""
from __future__ import annotations

import ast
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType, SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import lab_adapter_registry  # noqa: E402
import lab_registry  # noqa: E402
from lab_adapter_lab05peak import (  # noqa: E402
    HOST_DEADLINES_SECONDS,
    Adapter,
    _capture,
)
from lab_adapter_registry import resolve_adapter  # noqa: E402
from lab_registry import (  # noqa: E402
    LabRegistryError,
    get_lab,
    load_registry,
    validate_document_schema,
)
from lab_runner_adapter import Adapter as GenericAdapter  # noqa: E402
from project_config import ProjectConfigError  # noqa: E402
from server_log import redact_line  # noqa: E402

import importlib.util

_PARSER_PATH = ROOT / "client/labs/lab-05-transition-proof/tools/parse_lab05_peak.py"
_parser_spec = importlib.util.spec_from_file_location("parse_lab05_peak_under_test", _PARSER_PATH)
parser = importlib.util.module_from_spec(_parser_spec)
sys.modules[_parser_spec.name] = parser
_parser_spec.loader.exec_module(parser)

_LAB05_PROJECT = "client/labs/lab-05-transition-proof"
_INPUTS = ["P-alloc-fail", "P-stale-callback", "P-cancel"]


def _trace(scenario, terminal, operation_id, control="persistent", reason="ok", kind=None, **extra):
    cls = "exploratory" if scenario in {"P-overlap", "P-replace"} else "decided"
    fields = {
        "schema": "lab05-peak-trace/v1",
        "scenario": scenario,
        "control": control,
        "terminal": terminal,
        "operation_id": operation_id,
        "terminal_count": 1,
        "live_image_count": 1,
        "class": cls,
        "reason": reason,
        "temp_image_bytes": 28800,
        "lvgl_high_water_bytes": "unmeasured",
        "lvgl_largest_free_bytes": "unmeasured",
        "internal_largest_free_bytes": "unmeasured",
        "psram_largest_free_bytes": "unmeasured",
        "anim_frame_us": "unmeasured",
        "metrics_provenance": "unmeasured",
    }
    if kind is not None:
        fields["kind"] = kind
    fields.update(extra)
    return "LAB05_PEAK_TRACE " + " ".join(f"{key}={value}" for key, value in fields.items())


def _serial(*, control="persistent", mode="scripted", cancel="cancelled", harness=True, ready=True, overlap="success"):
    lines = []
    if ready:
        lines.append(
            f"LAB05_PEAK_READY build=lab05-peak-v1 mode={mode} control={control} server=none"
        )
    lines.append(_trace("P-overlap", overlap, 1, control, anim_frame_us="10,20,30,40", anim_frame_us_count=4))
    lines.append(_trace("P-replace", "failure", 2, control, reason="alloc"))
    lines.append(_trace("P-alloc-fail", "failure", 3, control, reason="alloc"))
    lines.append(_trace("P-stale-callback", "stale", 4, control, reason="stale"))
    lines.append(_trace("P-cancel", cancel, 5, control, reason="cancel"))
    if harness and mode == "scripted":
        lines.append(f"LAB05_PEAK_HARNESS_DONE status=complete control={control}")
    return "\n".join(lines) + "\n"


def _entry(lab_id, slug, project, adapter=None, variants=None):
    manual = {"build_dir": f"build/labs/fixture-{slug}-manual"}
    entry = {
        "lab_id": lab_id,
        "evidence_slug": slug,
        "project_dir": project,
        "variants": variants or {"default": "manual", "manual": manual},
        "run": {"instructions": "Exercise the registered lab."},
    }
    if adapter:
        entry["adapter"] = adapter
    return entry


def _lifecycle_variants():
    return {
        "default": "manual",
        "manual": {"build_dir": "build/labs/lab-05-transition-proof-manual"},
        "scripted": {"build_dir": "build/labs/lab-05-transition-proof-scripted"},
    }


def _peak_variants():
    return {
        "default": "manual",
        "manual": {"build_dir": "build/labs/lab-05-transition-proof-peak-manual"},
        "scripted": {"build_dir": "build/labs/lab-05-transition-proof-peak-scripted"},
    }


class Lab05PeakRegistryTests(unittest.TestCase):
    def test_lab05peak_registry_gates(self):
        # (1) Frozen schema enum, then the live enum that still contains lab04.
        document = {
            "document": "family-link.labs-registry/v1",
            "labs": {
                "lab05peak": {
                    "adapter": "lab05peak",
                    "project_dir": _LAB05_PROJECT,
                    "variants": {
                        "default": "manual",
                        "manual": {"build_dir": "build/labs/lab-05-transition-proof-peak-manual"},
                    },
                }
            },
        }
        live_schema = json.loads((ROOT / "config/labs/labs.registry.schema.v1.json").read_text(encoding="utf-8"))
        frozen_schema = json.loads(json.dumps(live_schema))
        frozen_schema["$defs"]["lab_entry"]["properties"]["adapter"]["enum"] = [
            "generic", "lab02", "lab03", "lab10",
        ]
        with tempfile.TemporaryDirectory() as tmp:
            frozen_path = Path(tmp) / "old-schema.json"
            frozen_path.write_text(json.dumps(frozen_schema), encoding="utf-8")
            with self.assertRaises(LabRegistryError):
                validate_document_schema(document, schema_path=frozen_path)
        live_enum = live_schema["$defs"]["lab_entry"]["properties"]["adapter"]["enum"]
        for key in ("generic", "lab02", "lab03", "lab04", "lab10", "lab05peak"):
            self.assertIn(key, live_enum)
        validate_document_schema(document)

        # (2) Frozen allowlist double, then the live map.
        frozen_allowlist = {
            "generic": "lab_runner_adapter",
            "lab02": "lab_adapter_lab02",
            "lab03": "lab_adapter_lab03",
            "lab04": "lab_adapter_lab04",
            "lab10": "lab_adapter_lab10",
        }
        peak_spec = SimpleNamespace(adapter_key="lab05peak", make_name="lab05peak")
        with patch.dict(lab_adapter_registry._ADAPTER_MODULES, frozen_allowlist, clear=True):
            with self.assertRaisesRegex(LabRegistryError, "Unsupported adapter key"):
                resolve_adapter(peak_spec)
        self.assertEqual(lab_adapter_registry._ADAPTER_MODULES["lab04"], "lab_adapter_lab04")
        self.assertEqual(
            resolve_adapter(get_lab("lab05peak")).__class__.__module__,
            "lab_adapter_lab05peak",
        )

        # (3) Uniqueness is off for the historical double, then enforced.
        import yaml

        shared = {
            "document": "family-link.labs-registry/v1",
            "labs": {
                "alpha": _entry("LAB-02", "lab-02", "client/labs/lab-02-bounded-inbox"),
                "beta": _entry("LAB-02", "lab-02-share", "client/labs/lab-02-bounded-inbox"),
            },
        }
        allowed = {
            "document": "family-link.labs-registry/v1",
            "labs": {
                "lab04": _entry("LAB-04", "lab-04", "client/labs/lab-04-sketch-format", adapter="lab04"),
                "lab05": _entry("LAB-05", "lab-05", _LAB05_PROJECT, variants=_lifecycle_variants()),
                "lab05peak": _entry(
                    "LAB-05", "lab-05-peak", _LAB05_PROJECT, adapter="lab05peak", variants=_peak_variants()
                ),
            },
        }
        wrong_slug = json.loads(json.dumps(allowed))
        wrong_slug["labs"]["lab05peak"]["evidence_slug"] = "lab-05-other"
        wrong_adapter = json.loads(json.dumps(allowed))
        wrong_adapter["labs"]["lab05peak"]["adapter"] = "lab04"
        third = json.loads(json.dumps(allowed))
        third["labs"]["lab05extra"] = _entry("LAB-05", "lab-05-extra", "client/labs/lab-04-sketch-format")
        extra_variant = json.loads(json.dumps(allowed))
        extra_variant["labs"]["lab05"]["variants"]["replace"] = {
            "build_dir": "build/labs/lab-05-transition-proof-peak-replace"
        }
        shared_slug = {
            "document": "family-link.labs-registry/v1",
            "labs": {
                "one": _entry("LAB-04", "lab-04", "client/labs/lab-04-sketch-format", adapter="lab04"),
                "two": _entry("LAB-02", "lab-04", "client/labs/lab-02-bounded-inbox", adapter="lab02"),
            },
        }

        def write(payload):
            handle = tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False, encoding="utf-8")
            yaml.safe_dump(payload, handle, sort_keys=False)
            handle.close()
            return Path(handle.name)

        shared_path = write(shared)
        try:
            with patch("lab_registry.enforce_unique_registration", lambda specs: None):
                self.assertEqual(lab_registry.main(["--file", str(shared_path), "validate"]), 0)
            self.assertNotEqual(lab_registry.main(["--file", str(shared_path), "validate"]), 0)
            allowed_path = write(allowed)
            self.assertEqual(lab_registry.main(["--file", str(allowed_path), "validate"]), 0)
            loaded = load_registry(allowed_path)
            self.assertEqual(loaded["lab05"].adapter_key, "generic")
            self.assertEqual(loaded["lab05peak"].adapter_key, "lab05peak")
            self.assertEqual(set(loaded["lab05"].variants), {"manual", "scripted"})
            for payload in (wrong_slug, wrong_adapter, third, extra_variant, shared_slug):
                path = write(payload)
                self.assertNotEqual(lab_registry.main(["--file", str(path), "validate"]), 0)
        finally:
            shared_path.unlink(missing_ok=True)

        live = load_registry()
        self.assertEqual(live["lab05"].adapter_key, "generic")
        self.assertIsNone(live["lab05"].trace_parser)
        self.assertEqual(set(live["lab05"].variants), {"manual", "scripted"})
        self.assertEqual(live["lab05peak"].adapter_key, "lab05peak")
        self.assertEqual(live["lab05peak"].evidence_slug, "lab-05-peak")
        self.assertEqual(live["lab05peak"].project_dir.as_posix(), _LAB05_PROJECT)
        self.assertTrue(live["lab05peak"].flash_by_default)
        self.assertIn("lab04", live)
        self.assertEqual(live["lab04"].adapter_key, "lab04")
        self.assertNotIn("peak", live["lab05"].variants)
        self.assertNotIn("scripted-replace", live["lab05"].variants)


class Lab05PeakParserTests(unittest.TestCase):
    def test_parser_does_not_import_lab10_aggregate_metrics(self):
        source = _PARSER_PATH.read_text(encoding="utf-8")
        self.assertNotIn("aggregate_metrics", source)
        tree = ast.parse(source)
        modules = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                modules.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                modules.append(node.module)
        self.assertFalse(any("aggregate_metrics" in name for name in modules))

    def test_nearest_rank_uses_real_samples_only(self):
        empty = parser.nearest_rank_summary([])
        self.assertEqual(empty["sample_count"], 0)
        self.assertIsNone(empty["p50"])
        self.assertIsNone(empty["p95"])
        self.assertIsNone(empty["p99"])
        self.assertIsNone(empty["max"])
        self.assertTrue(empty["missing_reason"])
        self.assertEqual(empty["provenance"], "unmeasured/blocked")
        skipped = parser.nearest_rank_summary([float("nan"), float("inf")])
        self.assertEqual(skipped["sample_count"], 0)
        self.assertIsNone(skipped["max"])
        ranked = parser.nearest_rank_summary([40, 10, 30, 20])
        self.assertEqual(ranked["p50"], 20)
        self.assertEqual(ranked["p95"], 40)
        self.assertEqual(ranked["p99"], 40)
        self.assertEqual(ranked["max"], 40)
        self.assertEqual(ranked["sample_count"], 4)
        self.assertEqual(ranked["provenance"], "host")
        single = parser.nearest_rank_summary([7])
        self.assertEqual([single["p50"], single["p95"], single["p99"], single["max"]], [7, 7, 7, 7])

    def test_metrics_document_matches_run_id_and_percentiles(self):
        result = parser.parse(_serial(), "hostcheck-parser-01", mode="scripted", variant="scripted")
        self.assertEqual(result["document"], "family-link.lab-metrics/v2")
        self.assertEqual(result["run_id"], "hostcheck-parser-01")
        series = result["lab_metrics"]["series"]["anim_frame_us"]
        self.assertEqual(series["p50"], 20)
        self.assertEqual(series["max"], 40)
        self.assertEqual(series["provenance"], "host")
        absent = result["lab_metrics"]["series"]["lvgl_high_water_bytes"]
        self.assertIsNone(absent["p50"])
        self.assertIsNone(absent["max"])
        self.assertEqual(absent["sample_count"], 0)
        self.assertTrue(absent["missing_reason"])
        self.assertEqual(absent["provenance"], "unmeasured/blocked")
        self.assertNotEqual(absent["p50"], 0)
        encoded = json.dumps(result)
        self.assertEqual(result["evidence_provenance"], "host")
        self.assertNotIn("hardware", encoded)

    def test_device_capture_context_sets_hardware_provenance(self):
        result = parser.parse(
            _serial(),
            "hostcheck-device-01",
            mode="scripted",
            variant="scripted",
            capture_context={
                "kind": "device_serial_capture",
                "target_id": "audrey-kit",
                "post_reset": True,
            },
        )
        self.assertEqual(result["evidence_provenance"], "hardware")
        self.assertEqual(result["provenance_context"]["target_id"], "audrey-kit")
        self.assertEqual(result["lab_metrics"]["series"]["anim_frame_us"]["provenance"], "hardware")
        absent = result["lab_metrics"]["series"]["lvgl_high_water_bytes"]
        self.assertEqual(absent["sample_count"], 0)
        self.assertEqual(absent["provenance"], "unmeasured/blocked")
        with self.assertRaises(parser.ParseError):
            parser.parse(
                _serial(),
                "hostcheck-device-02",
                mode="scripted",
                capture_context={"kind": "host_serial_observation", "post_reset": True},
            )

    def test_lab05_peak_experiment_inputs(self):
        passed = parser.parse(_serial(), "hostcheck-parser-02", mode="scripted", variant="scripted")
        metrics = passed["lab_metrics"]
        self.assertEqual(metrics["experiment_inputs"], _INPUTS)
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        for scenario_id in ("P-overlap", "P-replace"):
            self.assertEqual(metrics["scenarios"][scenario_id]["class"], "exploratory")
            self.assertIn("observation", metrics["scenarios"][scenario_id])
            self.assertNotIn(scenario_id, metrics["experiment_inputs"])
        self.assertNotIn("workflow_status", passed)
        failed = parser.parse(
            _serial(cancel="success"), "hostcheck-parser-03", mode="scripted", variant="scripted"
        )
        self.assertEqual(failed["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["P-overlap"]["class"], "exploratory")
        decided_fail = {
            "P-alloc-fail": {"terminal": "failure", "reason": "alloc", "terminal_count": 1},
            "P-stale-callback": {"terminal": "stale", "terminal_count": 1},
            "P-cancel": {"terminal": "success", "terminal_count": 1},
            "P-overlap": {"class": "exploratory", "observation": {"terminal": "success"}},
        }
        decided_ok = {
            "P-alloc-fail": {"terminal": "failure", "reason": "alloc", "terminal_count": 1},
            "P-stale-callback": {"terminal": "stale", "terminal_count": 1},
            "P-cancel": {"terminal": "cancelled", "terminal_count": 1},
            "P-replace": {"class": "exploratory", "observation": {"terminal": "failure"}},
        }
        self.assertEqual(parser.classify_experiment(decided_fail, _INPUTS), "FAIL")
        self.assertEqual(parser.classify_experiment(decided_ok, _INPUTS), "PASS")
        self.assertEqual(parser.classify_experiment(decided_ok, []), "UNMEASURED")
        self.assertIn("observation", decided_fail["P-overlap"])
        self.assertIn("observation", decided_ok["P-replace"])

    def test_parser_rejects_malformed_missing_duplicate_overflow_and_secrets(self):
        good = _serial()
        cases = [
            "LAB05_PEAK_TRACE this is not key value\n",
            good.replace("operation_id=1", "operation_id=1 operation_id=1", 1),
            good.replace("operation_id=1", "", 1),
            good.replace("operation_id=2", "operation_id=1", 1),
            good.replace("operation_id=1", "operation_id=18446744073709551616", 1),
            good + "PIN=1234\n",
            good + "LAB05_PEAK_TRACE payload_b64=abc media_payload=raw\n",
        ]
        for value in cases:
            with self.subTest(value=value[-60:]), self.assertRaises(parser.ParseError):
                parser.parse(value, "hostcheck-parser-04", mode="scripted", variant="scripted")
        with self.assertRaises(parser.ParseError):
            parser.parse(good, "hostcheck-parser-04", mode="scripted", post_reset=False)

    def test_worker_rows_do_not_duplicate_decided_or_inflate_repetitions(self):
        lines = [
            "LAB05_PEAK_READY build=lab05-peak-v1 mode=scripted control=persistent server=none"
        ]
        operation_id = 1
        for _rep in range(20):
            lines.append(_trace("P-overlap", "success", operation_id, kind="repetition"))
            operation_id += 1
            lines.append(_trace("P-overlap", "success", operation_id, kind="worker", reason="done"))
            operation_id += 1
            lines.append(_trace("P-overlap", "success", operation_id, kind="worker", reason="done"))
            operation_id += 1
        lines.append(_trace("P-alloc-fail", "failure", operation_id, reason="alloc", kind="repetition"))
        operation_id += 1
        lines.append(_trace("P-stale-callback", "stale", operation_id, reason="stale", kind="repetition"))
        operation_id += 1
        lines.append(_trace("P-cancel", "success", operation_id, reason="done", kind="worker"))
        operation_id += 1
        lines.append(_trace("P-cancel", "success", operation_id, reason="done", kind="worker"))
        operation_id += 1
        lines.append(_trace("P-cancel", "cancelled", operation_id, reason="cancel", kind="repetition"))
        lines.append("LAB05_PEAK_HARNESS_DONE status=complete control=persistent")
        result = parser.parse(
            "\n".join(lines) + "\n",
            "hostcheck-workers",
            mode="scripted",
            variant="scripted",
        )
        metrics = result["lab_metrics"]
        self.assertEqual(metrics["experiment_verdict"], "PASS")
        self.assertEqual(metrics["control"], "persistent")
        overlap = metrics["scenarios"]["P-overlap"]
        self.assertEqual(overlap["observation"]["repetition_count"], 20)
        self.assertEqual(overlap["class"], "exploratory")
        cancel = metrics["scenarios"]["P-cancel"]
        self.assertEqual(cancel["terminal"], "cancelled")
        self.assertEqual(cancel["terminal_count"], 1)
        self.assertEqual(sum(1 for item in cancel["operations"] if item["kind"] == "worker"), 2)
        self.assertEqual(sum(1 for item in cancel["operations"] if item["kind"] == "repetition"), 1)
        alloc = metrics["scenarios"]["P-alloc-fail"]
        self.assertEqual(alloc["observation"]["control"], "persistent")

        kindless = [
            "LAB05_PEAK_READY build=lab05-peak-v1 mode=scripted control=persistent server=none",
            _trace("P-alloc-fail", "failure", 1, reason="alloc"),
            _trace("P-stale-callback", "stale", 2, reason="stale"),
            _trace("P-cancel", "success", 3, reason="done"),
            _trace("P-cancel", "success", 4, reason="done"),
            _trace("P-cancel", "cancelled", 5, reason="cancel"),
            "LAB05_PEAK_HARNESS_DONE status=complete control=persistent",
        ]
        legacy = parser.parse(
            "\n".join(kindless) + "\n",
            "hostcheck-cancel-legacy",
            mode="scripted",
            variant="scripted",
        )
        legacy_cancel = legacy["lab_metrics"]["scenarios"]["P-cancel"]
        self.assertEqual(legacy_cancel["terminal"], "cancelled")
        self.assertEqual(legacy_cancel["terminal_count"], 1)
        self.assertEqual(legacy["lab_metrics"]["experiment_verdict"], "PASS")
        self.assertEqual(legacy["lab_metrics"]["control"], "persistent")

    def test_alloc_fail_persistent_control_does_not_demote(self):
        passed = parser.parse(_serial(), "hostcheck-alloc-control", mode="scripted", variant="scripted")
        self.assertEqual(passed["lab_metrics"]["experiment_verdict"], "PASS")
        self.assertEqual(passed["lab_metrics"]["control"], "persistent")
        self.assertEqual(
            passed["lab_metrics"]["scenarios"]["P-alloc-fail"]["observation"]["control"],
            "persistent",
        )
        mixed = _serial().replace(
            "scenario=P-alloc-fail control=persistent",
            "scenario=P-alloc-fail control=replace",
            1,
        )
        demoted = parser.parse(mixed, "hostcheck-alloc-mixed", mode="scripted", variant="scripted")
        self.assertEqual(demoted["lab_metrics"]["experiment_verdict"], "UNMEASURED")
        self.assertIsNone(demoted["lab_metrics"]["control"])

    def test_replace_arm_without_decided_rows_is_unmeasured(self):
        lines = [
            "LAB05_PEAK_READY build=lab05-peak-replace-v1 mode=scripted control=replace server=none"
        ]
        for operation_id in range(1, 21):
            lines.append(_trace("P-replace", "success", operation_id, control="replace", kind="repetition"))
        lines.append("LAB05_PEAK_HARNESS_DONE status=complete control=replace")
        result = parser.parse(
            "\n".join(lines) + "\n",
            "hostcheck-replace-only",
            mode="scripted",
            variant="scripted-replace",
        )
        self.assertEqual(result["lab_metrics"]["experiment_verdict"], "UNMEASURED")
        self.assertNotEqual(result["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(result["lab_metrics"]["control"], "replace")
        self.assertEqual(result["lab_metrics"]["experiment_inputs"], _INPUTS)
        self.assertIn("observation", result["lab_metrics"]["scenarios"]["P-replace"])
        violated = [
            "LAB05_PEAK_READY build=lab05-peak-replace-v1 mode=scripted control=replace server=none",
            _trace("P-replace", "success", 1, control="replace", kind="repetition"),
            _trace("P-alloc-fail", "failure", 2, control="replace", reason="alloc", kind="repetition"),
            _trace("P-stale-callback", "stale", 3, control="replace", reason="stale", kind="repetition"),
            _trace("P-cancel", "success", 4, control="replace", reason="cancel", kind="repetition"),
            "LAB05_PEAK_HARNESS_DONE status=complete control=replace",
        ]
        failed = parser.parse(
            "\n".join(violated) + "\n",
            "hostcheck-replace-violation",
            mode="scripted",
            variant="scripted-replace",
        )
        self.assertEqual(failed["lab_metrics"]["experiment_verdict"], "FAIL")


class Lab05PeakAdapterTests(unittest.TestCase):
    def test_adapter_imports_and_deadlines(self):
        source = (ROOT / "scripts/lab_adapter_lab05peak.py").read_text(encoding="utf-8")
        tree = ast.parse(source)
        modules = []
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                modules.extend(alias.name for alias in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module:
                modules.append(node.module)
        self.assertNotIn("lab_adapter_lab02", modules)
        self.assertNotIn("lab_run_evidence", modules)
        self.assertNotIn("socket", modules)
        self.assertEqual(
            HOST_DEADLINES_SECONDS,
            {"startup": 10, "health": 10, "seed": 10, "readiness": 15, "action": 5, "cleanup": 5},
        )

    def test_s10_redact_line_redacts_url_password_and_bearer(self):
        raw = b"see https://example.test/secret password=hunter2 Bearer abcdefghijkl"
        redacted = redact_line(raw)
        self.assertIn(b"[REDACTED_URL]", redacted)
        self.assertNotIn(b"https://", redacted)
        self.assertIn(b"password=[REDACTED]", redacted)
        self.assertNotIn(b"hunter2", redacted)
        self.assertIn(b"Bearer [REDACTED]", redacted)
        self.assertNotIn(b"abcdefghijkl", redacted)

    def test_s02_manifest_and_s11_not_applicable(self):
        manifest = json.loads(
            (ROOT / "client/labs/lab-05-transition-proof/manifest.lab.json").read_text(encoding="utf-8")
        )
        for key in (
            "document",
            "lab_id",
            "firmware_path",
            "server_path",
            "fixture_schema",
            "fixture_revision",
            "protocol",
            "trace_schema",
        ):
            self.assertIn(key, manifest)
        self.assertEqual(manifest["document"], "family-link.lab-manifest/v1")
        self.assertEqual(manifest["lab_id"], "LAB-05")
        self.assertEqual(manifest["server_path"], "none")
        self.assertEqual(manifest["lab_revision"], "v1-proof")
        self.assertEqual(manifest["peak"]["lab_revision"], "v1-peak")
        self.assertEqual(manifest["peak"]["evidence_slug"], "lab-05-peak")
        self.assertEqual(manifest["peak"]["server_path"], "none")
        # S-11 is not applicable: this registration has server_path none.
        self.assertEqual(manifest["peak"]["server_path"], "none")

    def test_scripted_variant_selection_and_local_backend_binds_no_port(self):
        spec = get_lab("lab05peak")
        sockets = []

        def forbid_socket(*_args, **_kwargs):
            sockets.append("socket")
            raise AssertionError("TCP socket constructed")

        with patch("socket.socket", forbid_socket):
            with self.assertRaises(ProjectConfigError):
                Adapter().run(
                    SimpleNamespace(),
                    None,
                    spec,
                    SimpleNamespace(backend="local", mode="scripted", variant="", seconds=1, run_id="x"),
                    mode="scripted",
                )
        self.assertEqual(sockets, [])
        code, manifest, metrics = self._run("hostcheck-variant-scripted", _serial(), variant="")
        self.assertEqual(code, 0)
        self.assertEqual(manifest["capture"]["variant"], "scripted")
        self.assertEqual(metrics["lab_metrics"]["control"], "persistent")
        self.assertNotEqual(manifest["workflow_status"], "FAIL")
        code, manifest, metrics = self._run(
            "hostcheck-variant-replace",
            _serial(control="replace"),
            variant="scripted-replace",
        )
        self.assertEqual(manifest["capture"]["variant"], "scripted-replace")
        self.assertEqual(metrics["lab_metrics"]["control"], "replace")
        lifecycle = get_lab("lab05")
        self.assertNotEqual(
            spec.variants["scripted-replace"].build_dir.as_posix(),
            lifecycle.variants["scripted"].build_dir.as_posix(),
        )
        self.assertEqual(manifest["evidence_slug"], "lab-05-peak")

    def test_lab05_scripted_stays_unmeasured_without_peak_evidence(self):
        spec = get_lab("lab05")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)

            def capture_lab(*_args, **kwargs):
                path = root / "logs" / "device" / "labs" / "lab-05" / kwargs["run_id"] / "serial.txt"
                path.parent.mkdir(parents=True)
                path.write_text("LAB05_READY\n", encoding="utf-8")
                self.assertFalse(kwargs["flash"])
                return path

            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_a, **_k: False,
                resolve_run_target=lambda *_a, **_k: SimpleNamespace(kit_id="audrey-kit", device="COM-AUDREY"),
                ensure_lab_built=lambda *_a, **_k: root / "build",
                capture_lab=capture_lab,
            )
            args = SimpleNamespace(
                variant="",
                seconds=1,
                kit="",
                port="",
                run_id="hostcheck-lab05-scripted",
                no_build=False,
                no_flash=True,
                flash=False,
                backend="managed-http",
                mode="scripted",
            )
            code = GenericAdapter().run(host, None, spec, args, mode="scripted")
            manifest_path = (
                root / "docs/labs/evidence/lab-05/runs/hostcheck-lab05-scripted/run-manifest.json"
            )
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            self.assertEqual(code, 0)
            self.assertEqual(manifest["evidence_slug"], "lab-05")
            self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")
            self.assertEqual(manifest["workflow_status"], "PASS")
            self.assertNotEqual(manifest["experiment_verdict"], "PASS")
            serial = (root / "logs/device/labs/lab-05/hostcheck-lab05-scripted/serial.txt").read_text(encoding="utf-8")
            self.assertNotIn("LAB05_HARNESS_DONE", serial)
            self.assertNotIn("LAB05_PEAK_READY", serial)
            self.assertFalse((root / "docs/labs/evidence/lab-05-peak").exists())
            self.assertFalse((root / "logs/device/labs/lab-05-peak").exists())

    def test_post_reset_marker_cleanup_interrupt_and_provenance(self):
        code, manifest, metrics = self._run("hostcheck-peak-pass", _serial())
        self.assertEqual(code, 0)
        self.assertEqual(manifest["workflow_status"], "PASS")
        self.assertEqual(manifest["experiment_verdict"], "PASS")
        self.assertEqual(manifest["evidence_slug"], "lab-05-peak")
        self.assertEqual(manifest["server"]["path"], "none")
        encoded = json.dumps(manifest)
        self.assertNotIn("bind_host", encoded)
        self.assertNotIn("bind_port", encoded)
        self.assertEqual(manifest["capture"]["stages"]["service"], "not_configured")
        self.assertEqual(manifest["capture"]["stages"]["scenario"], "not_configured")
        self.assertEqual(metrics["evidence_provenance"], "hardware")
        self.assertEqual(metrics["provenance_context"]["kind"], "device_serial_capture")
        self.assertEqual(metrics["provenance_context"]["target_id"], "audrey-kit")
        self.assertTrue(metrics["provenance_context"]["post_reset"])
        for series in metrics["lab_metrics"]["series"].values():
            if series["sample_count"] == 0:
                self.assertEqual(series["provenance"], "unmeasured/blocked")
            else:
                self.assertEqual(series["provenance"], "hardware")

        pre = b"PRE_RESET_ONLY LAB05_PEAK_HARNESS_DONE status=complete control=persistent\n"
        post = _serial(harness=False).encode("utf-8")
        code, manifest, _metrics, serial_text = self._run_capture(
            "hostcheck-prereset", pre=pre, post=post
        )
        self.assertNotEqual(manifest["workflow_status"], "PASS")
        self.assertEqual(manifest["experiment_verdict"], "UNMEASURED")
        self.assertNotIn("PRE_RESET_ONLY", serial_text)
        self.assertNotIn("LAB05_PEAK_HARNESS_DONE", serial_text)
        self.assertNotEqual(code, 0)

        code, manifest, metrics = self._run(
            "hostcheck-cleanup", _serial(), cleanup_error=RuntimeError("cleanup")
        )
        self.assertEqual(manifest["workflow_status"], "FAIL")
        self.assertNotEqual(manifest["experiment_verdict"], "PASS")
        self.assertNotEqual(code, 0)
        self.assertIn("workflow_status", manifest)
        self.assertIn("experiment_verdict", manifest)

        code, manifest, _metrics = self._run(
            "hostcheck-interrupt", _serial(), capture_error=KeyboardInterrupt()
        )
        self.assertEqual(manifest["workflow_status"], "INCOMPLETE")
        self.assertNotEqual(manifest["experiment_verdict"], "PASS")
        self.assertNotEqual(code, 0)

        code, manifest, metrics = self._run("hostcheck-decided-fail", _serial(cancel="success"))
        self.assertEqual(manifest["workflow_status"], "PASS")
        self.assertEqual(manifest["experiment_verdict"], "FAIL")

    def _run(self, run_id, serial, variant="", cleanup_error=None, capture_error=None):
        code, manifest, metrics, _serial_text = self._run_capture(
            run_id,
            pre=b"",
            post=serial.encode("utf-8"),
            variant=variant,
            cleanup_error=cleanup_error,
            capture_error=capture_error,
        )
        return code, manifest, metrics

    def _run_capture(self, run_id, *, pre, post, variant="", cleanup_error=None, capture_error=None):
        spec = get_lab("lab05peak")
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            host = SimpleNamespace(
                ROOT=root,
                _flash_from_env_and_args=lambda *_a, **_k: False,
                resolve_run_target=lambda *_a, **_k: SimpleNamespace(kit_id="audrey-kit", device="COM-AUDREY"),
                build_lab=lambda *_a, **_k: root / "build",
            )
            args = SimpleNamespace(
                backend="managed-http",
                mode="scripted",
                variant=variant,
                seconds=1,
                kit="",
                port="",
                run_id=run_id,
                no_build=False,
                no_flash=True,
                flash=False,
            )
            serial_holder = {}

            class FakeSerial:
                def __init__(self, _port, *_args, **_kwargs):
                    self.dtr = False
                    self.rts = False
                    self.reset_calls = 0
                    self.reset_while_armed = False
                    self._phase = "pre"
                    self._given = False
                    self.closed = False

                def reset_input_buffer(self):
                    self.reset_calls += 1
                    self.reset_while_armed = self.rts is True
                    self._phase = "post"
                    self._given = False

                def read(self, _size):
                    if self._given:
                        return b""
                    self._given = True
                    return pre if self._phase == "pre" else post

                def close(self):
                    self.closed = True

            serial_module = ModuleType("serial")
            serial_module.Serial = FakeSerial
            patches = [
                patch.dict(sys.modules, {"serial": serial_module}),
                patch("lab_adapter_lab05peak.time.sleep"),
                patch("lab_adapter_lab05peak.HARNESS_TIMEOUT_SECONDS", 0.05),
                patch("lab_adapter_lab05peak.BOOT_TIMEOUT_SECONDS", 0.05),
            ]
            if cleanup_error is not None:
                patches.append(patch("lab_adapter_lab05peak._cleanup", side_effect=cleanup_error))
            if capture_error is not None:
                patches.append(patch("lab_adapter_lab05peak._capture", side_effect=capture_error))
            for item in patches:
                item.start()
            try:
                code = Adapter().run(host, None, spec, args, mode="scripted")
            finally:
                for item in reversed(patches):
                    item.stop()
            manifest_path = root / "docs/labs/evidence/lab-05-peak/runs" / run_id / "run-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            metrics_path = root / "docs/labs/evidence/lab-05-peak/runs" / run_id / "metrics.json"
            metrics = json.loads(metrics_path.read_text(encoding="utf-8")) if metrics_path.is_file() else None
            serial_path = root / "logs/device/labs/lab-05-peak" / run_id / "serial.txt"
            serial_text = serial_path.read_text(encoding="utf-8") if serial_path.is_file() else ""
            serial_holder["text"] = serial_text
            self.assertFalse((ROOT / "docs/labs/evidence/lab-05-peak/runs" / run_id).exists())
            return code, manifest, metrics, serial_holder["text"]


class Lab05PeakCaptureTests(unittest.TestCase):
    def test_sensitive_line_is_not_written(self):
        class FakeSerial:
            def __init__(self, _port, *_args, **_kwargs):
                self.dtr = False
                self.rts = False
                self._reset = False
                self._given = False

            def reset_input_buffer(self):
                self._reset = True
                self._given = False

            def read(self, _size):
                if not self._reset or self._given:
                    return b""
                self._given = True
                return b"PIN=1234\n"

            def close(self):
                pass

        with tempfile.TemporaryDirectory() as tmp, patch("lab_adapter_lab05peak.time.sleep"):
            fake_module = ModuleType("serial")
            fake_module.Serial = FakeSerial
            host = SimpleNamespace(ROOT=Path(tmp))
            with patch.dict(sys.modules, {"serial": fake_module}):
                with self.assertRaises(parser.ParseError):
                    _capture(
                        host,
                        SimpleNamespace(device="COM-AUDREY"),
                        "hostcheck-secret",
                        1,
                        "scripted",
                        "persistent",
                        parser,
                    )
            text = (Path(tmp) / "logs/device/labs/lab-05-peak/hostcheck-secret/serial.txt").read_text(encoding="utf-8")
            self.assertNotIn("1234", text)


if __name__ == "__main__":
    unittest.main()
