"""Focused LAB-06 parser and adapter contract tests."""
from __future__ import annotations

import importlib.util
import json
import shutil
import sys
import tempfile
import types
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module

parser = _load("lab06_test_parser", ROOT / "client/labs/lab-06-stage-shell/tools/parse_lab_trace.py")
import lab_adapter_lab06 as adapter_module  # noqa: E402
from lab_registry import get_lab  # noqa: E402
from project_config import ProjectConfigError  # noqa: E402


def _row(event, **updates):
    defaults = {
        "scenario_terminal": {"schema":"lab06-trace/v1","event":"scenario_terminal","scenario":"H-data","class":"decided","operation_id":300,"generation":2,"stage_before":"roster","stage_after":"roster","terminal":"success","terminal_count":1,"reason":"panel_data_applied","update_level":0,"operation_count":1,"queue_overflow":0,"stale_handle_use":0,"widget_live":19,"timer_count":1,"anim_us":0,"build_us":10,"placeholder":0,"old_panel_text_visible":0},
        "transition_terminal": {"schema":"lab06-trace/v1","event":"transition_terminal","operation_id":1,"generation":1,"stage_before":"roster","stage_after":"pin","terminal":"success","terminal_count":1,"reason":"H-cycle enter","update_level":2,"widget_live":19,"timer_count":1,"anim_us":400000},
        "trace_summary": {"schema":"lab06-trace/v1","event":"trace_summary","scenario":"H-placeholder","class":"exploratory","placeholder":1,"experiment_inputs":parser.DECIDED,"scenario_pass_count":6,"scenario_fail_count":0,"operation_count":243,"queue_overflow":1,"stale_handle_use":0,"peak_panel_widgets":38,"timer_count":1,"performance":"UNMEASURED"},
        "queue_overflow": {"schema":"lab06-trace/v1","event":"queue_overflow","publication_id":8,"queue_overflow":1,"terminal":"failure","terminal_count":1,"reason":"overflow","probe":"bounded_queue_capacity_8"},
        "resource_sample": {"schema":"lab06-trace/v1","event":"resource_sample","reason":"harness_done","ledger_a":"lvgl_pool","lvgl_total_bytes":65536,"lvgl_free_bytes":50000,"lvgl_largest_free_bytes":40000,"lvgl_high_water_pct":25,"ledger_b":"heap_caps","internal_free_bytes":100000,"internal_largest_free_bytes":50000,"psram_free_bytes":200000,"psram_largest_free_bytes":100000,"provenance":"lv_mem_monitor+heap_caps;device"},
    }
    return {**defaults[event], **updates}


def _complete_serial():
    rows = []
    oid = 1
    rows.append(_row("transition_terminal", operation_id=oid))
    oid += 1
    for _ in range(120):
        rows.append(_row("scenario_terminal", scenario="H-cycle", operation_id=oid))
        oid += 1
        rows.append(_row("transition_terminal", operation_id=oid))
        oid += 1
    data_rows = [
        _row("scenario_terminal", scenario="H-data", operation_id=oid, update_level=0),
        _row("scenario_terminal", scenario="H-data", operation_id=oid+1, update_level=1),
        _row("scenario_terminal", scenario="H-data-during-exit", operation_id=oid+2, terminal="stale", reason="stale_panel", update_level=1),
        _row("scenario_terminal", scenario="H-level2-control", operation_id=oid+3, terminal="failure", reason="level2_on_data", update_level=2),
        _row("scenario_terminal", scenario="H-destroyed", operation_id=oid+4, terminal="stale", reason="stale_panel", stale_handle_use=0),
        _row("scenario_terminal", scenario="H-cancel", operation_id=oid+5, terminal="cancelled", reason="cancelled", stage_before="hangout", stage_after="hangout"),
        _row("scenario_terminal", scenario="H-placeholder", **{"class":"exploratory","operation_id":oid+6,"reason":"synthetic_only","placeholder":1}),
    ]
    rows.extend(data_rows)
    rows.extend([_row("queue_overflow"),_row("trace_summary")])
    return "\n".join(["LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1", *[json.dumps(row,separators=(",",":")) for row in rows], "LAB06_HARNESS_DONE status=complete"]) + "\n"


class ParserTests(unittest.TestCase):
    def parse(self, text=None, **kwargs):
        capture = {"kind":"device_serial_capture","target_id":"fixture-kit","post_reset":True,
                   "reset_performed":True,"ready_marker_verified":True,
                   "build_fingerprint":"sha256:fixture","fixture_sha256":"fixture"}
        return parser.parse_text(_complete_serial() if text is None else text, expected_mode="scripted", expected_variant="scripted", post_reset=True, capture_context=capture, expected_build_id="lab06-fw-scripted-v3", **kwargs)

    def test_complete_decided_inventory_negative_control_and_exploratory_placeholder(self):
        metrics = self.parse()
        lm = metrics["lab_metrics"]
        self.assertEqual(lm["experiment_inputs"], ["H-cycle","H-data","H-data-during-exit","H-level2-control","H-destroyed","H-cancel"])
        self.assertEqual(lm["experiment_verdict"], "PASS")
        self.assertEqual(len(lm["scenarios"]["H-cycle"]["operation_results"]), 240)
        self.assertEqual(lm["scenarios"]["H-level2-control"]["operation_results"][0]["reason"], "level2_on_data")
        self.assertEqual(lm["scenarios"]["H-placeholder"]["class"], "exploratory")
        self.assertEqual(lm["scenarios"]["H-placeholder"]["observation"]["placeholder"], 1)
        self.assertNotIn("H-placeholder", lm["experiment_inputs"])
        self.assertEqual(lm["performance_verdict"], "UNMEASURED")
        self.assertEqual(len(lm["queue_overflow_observations"]), 1)
        self.assertEqual(lm["queue_overflow_observations"][0]["reason"], "overflow")
        self.assertEqual(lm["scenarios"]["H-cancel"]["operation_results"][0]["terminal"], "cancelled")

    def test_valid_lvgl_percentage_and_partial_manual_capture(self):
        row = _row("resource_sample", reason="boot", lvgl_high_water_pct=19)
        text = "\n".join([
            "LAB06_READY build=lab06-fw-manual-v3 mode=manual server=none trace_schema=lab06-trace/v1",
            json.dumps(row, separators=(",", ":")),
            json.dumps(_row("scenario_terminal", scenario="H-data", operation_id=2), separators=(",", ":")),
        ])
        capture = {"kind":"device_serial_capture", "target_id":"fixture-kit",
                   "post_reset":True, "reset_performed":True,
                   "ready_marker_verified":True, "build_fingerprint":"sha256:fixture",
                   "fixture_sha256":"fixture"}
        metrics = parser.parse_text(text, expected_mode="manual", expected_variant="manual",
                                    post_reset=True, capture_context=capture,
                                    expected_build_id="lab06-fw-manual-v3")
        self.assertEqual(metrics["lab_metrics"]["experiment_verdict"], "INCOMPLETE")
        self.assertEqual(metrics["lab_metrics"]["series"]["lvgl_high_water_pct"]["worst"], 19)

        invalid = text.replace('"lvgl_high_water_pct":19', '"lvgl_high_water_pct":12320')
        with self.assertRaisesRegex(parser.ParseError, "invalid resource sample"):
            parser.parse_text(invalid, expected_mode="manual", expected_variant="manual",
                              post_reset=True, capture_context=capture,
                              expected_build_id="lab06-fw-manual-v3")


    def test_missing_sample_is_incomplete_and_empty_is_unmeasured(self):
        lines = [line for line in _complete_serial().splitlines() if not ('"scenario":"H-destroyed"' in line)]
        metrics_missing = self.parse("\n".join(lines)+"\n")
        self.assertEqual(metrics_missing["lab_metrics"]["experiment_verdict"], "INCOMPLETE")
        marker_only = "LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1\nLAB06_HARNESS_DONE status=complete\n"
        metrics = self.parse(marker_only)
        self.assertEqual(metrics["lab_metrics"]["experiment_verdict"], "UNMEASURED")
        self.assertEqual(parser._experiment_verdict([], metrics["lab_metrics"]["scenarios"], {}), "UNMEASURED")

    def test_host_parse_cannot_claim_post_reset_device_evidence(self):
        metrics = parser.parse_text(_complete_serial(), expected_mode="scripted", expected_variant="scripted",
                                    expected_build_id="lab06-fw-scripted-v3")
        self.assertEqual(metrics["capture"]["provenance"], "host_parse")
        self.assertEqual(metrics["capture"]["evidence_level"], "L1")
        self.assertFalse(metrics["capture"]["post_reset"])
        self.assertIsNone(metrics["capture"]["mode"])
        self.assertIsNone(metrics["capture"]["build_id"])
        self.assertEqual(metrics["lab_metrics"]["experiment_verdict"], "UNMEASURED")

    def test_serial_fixtures_reject_raw_pin_and_media_payload(self):
        for line in ("diagnostic pin=1234", "diagnostic media_payload=AAAA"):
            with self.subTest(line=line), self.assertRaises(parser.ParseError):
                self.parse(_complete_serial() + line + "\n")

    def test_exploratory_rows_never_change_decided_verdict(self):
        rows = _complete_serial().splitlines()
        decided_failure = []
        exploratory_success = []
        for line in rows:
            if line.startswith("{"):
                row = json.loads(line)
                if row.get("event") == "scenario_terminal" and row.get("scenario") == "H-data":
                    row["terminal"] = "failure"
                    row["reason"] = "decided_failure"
                decided_failure.append(json.dumps(row, separators=(",", ":")))
            else:
                decided_failure.append(line)
        failed = self.parse("\n".join(decided_failure) + "\n")
        self.assertEqual(failed["lab_metrics"]["experiment_verdict"], "FAIL")
        self.assertEqual(failed["lab_metrics"]["scenarios"]["H-placeholder"]["observation"]["operation_results"][0]["terminal"], "success")

        for line in rows:
            if line.startswith("{"):
                row = json.loads(line)
                if row.get("event") == "scenario_terminal" and row.get("scenario") == "H-placeholder":
                    row["terminal"] = "failure"
                    row["reason"] = "exploratory_failure"
                if row.get("event") == "trace_summary":
                    row["scenario_fail_count"] = 1
                exploratory_success.append(json.dumps(row, separators=(",", ":")))
            else:
                exploratory_success.append(line)
        passed = self.parse("\n".join(exploratory_success) + "\n")
        self.assertEqual(passed["lab_metrics"]["experiment_verdict"], "PASS")
        self.assertEqual(passed["lab_metrics"]["scenarios"]["H-placeholder"]["observation"]["operation_results"][0]["terminal"], "failure")

    def test_missing_mismatched_and_pre_reset_markers_fail_closed(self):
        serial = _complete_serial()
        with self.assertRaises(parser.ParseError):
            parser.parse_text(serial.replace("LAB06_READY", "LAB06_NOT_READY"), expected_mode="scripted", post_reset=True)
        with self.assertRaises(parser.ParseError):
            parser.parse_text(serial.replace("mode=scripted", "mode=manual"), expected_mode="scripted", post_reset=True)
        host_parse = parser.parse_text(serial, expected_mode="scripted", post_reset=False)
        self.assertFalse(host_parse["capture"]["post_reset"])
        self.assertEqual(host_parse["lab_metrics"]["experiment_verdict"], "UNMEASURED")
        with self.assertRaises(parser.ParseError):
            parser.parse_text(serial.replace("status=complete", "status=failure"), expected_mode="scripted", post_reset=True)

    def test_duplicate_malformed_overflow_and_operation_ids_rejected(self):
        marker = "LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1\n"
        done = "\nLAB06_HARNESS_DONE status=complete\n"
        with self.assertRaises(parser.ParseError):
            parser.parse_text(marker + '{"schema":"lab06-trace/v1","schema":"lab06-trace/v1","event":"queue_overflow"}' + done, expected_mode="scripted", post_reset=True)
        with self.assertRaises(parser.ParseError):
            parser.parse_text(marker + json.dumps({**_row("queue_overflow"),"publication_id":2**32}) + done, expected_mode="scripted", post_reset=True)
        bad = _row("scenario_terminal", operation_id=2**64)
        with self.assertRaises(parser.ParseError):
            parser.parse_text(marker + json.dumps(bad) + done, expected_mode="scripted", post_reset=True)
        duplicate = _row("scenario_terminal")
        with self.assertRaises(parser.ParseError):
            parser.parse_text(marker + json.dumps(duplicate) + "\n" + json.dumps(duplicate) + done, expected_mode="scripted", post_reset=True)
        with self.assertRaises(parser.ParseError):
            parser.parse_text(marker + '{"schema":"lab06-trace/v1","event":"scenario_terminal"}' + done, expected_mode="scripted", post_reset=True)


class FirmwareTraceContractTests(unittest.TestCase):
    def test_resource_and_scripted_operator_contracts(self):
        source = (ROOT / "client/labs/lab-06-stage-shell/main/lab06_fw.c").read_text()
        self.assertIn("(unsigned)monitor.used_pct", source)
        self.assertNotIn("(unsigned)monitor.max_used", source)
        click = source.split("static void click_event(lv_event_t *event)", 1)[1].split("static lv_obj_t *make_button", 1)[0]
        self.assertIn("if (s_fw.scripted) return;", click)
        timer_tick = source.split("static void timer_tick(lv_timer_t *timer)", 1)[1]
        action = timer_tick.index("lab06_animation_action(")
        cancel = timer_tick.index("if (action == LAB06_ANIMATION_CANCEL)")
        finish = timer_tick.index("if (action == LAB06_ANIMATION_FINISH || fraction >= 1.0f)")
        self.assertLess(action, cancel)
        self.assertLess(cancel, finish)
        begin = source.split("static bool begin_transition(", 1)[1].split("static void finish_transition", 1)[0]
        self.assertIn("lab06_cancel_transition_started(&s_fw.manual_cancel, s_fw.scripted)", begin)
        self.assertIn('"Cancel armed; reverse at 200 ms"', begin)
        self.assertIn("lab06_cancel_control_clear(\n            &s_fw.manual_cancel, s_fw.scripted)", begin)
        self.assertIn('"cancel cleared; retry"', begin)
        recovery = source.split("static void recover_transition_mount_failure(", 1)[1].split("static void mark_scenario", 1)[0]
        self.assertIn("lab06_cancel_control_clear(\n        &s_fw.manual_cancel, s_fw.scripted)", recovery)
        self.assertIn('"cancel cleared; retry"', recovery)
        process = source.split("static void process_ui_event(", 1)[1].split("static void process_stale_callback", 1)[0]
        generation_guard = process.split("switch (event->kind)", 1)[0]
        self.assertIn("s_fw.session.panels[slot].generation != event->generation", generation_guard)
        self.assertIn('"Stale queued input"', generation_guard)
        cancel_case = process.split("case UI_EVENT_CANCEL:", 1)[1].split("case UI_EVENT_STALE_CALLBACK:", 1)[0]
        self.assertIn("lab06_cancel_arm(&s_fw.manual_cancel, s_fw.scripted", cancel_case)
        self.assertIn('update_status("manual", "Cancel armed")', cancel_case)
        self.assertIn("if (scripted_cancel) s_fw.script_state = SCRIPT_CANCEL_WAIT;", timer_tick)
        self.assertIn("(!s_fw.scripted && s_fw.manual_cancel.transition_scheduled)", timer_tick)
        script_tick = source.split("static void script_tick(void)", 1)[1]
        cancel_state = script_tick.split("case SCRIPT_CANCEL_MID:", 1)[1].split("case SCRIPT_CANCEL_WAIT:", 1)[0]
        self.assertIn("if (s_fw.transition_complete)", cancel_state)
        self.assertIn("mark_scenario(false)", cancel_state)
        self.assertIn("CANCEL PROBE", source)
        self.assertIn("Cancel → Exit+Data → auto reverse", source)
        self.assertIn('make_button(lower, "1  Cancel"', source)
        self.assertIn('make_button(lower, "2  Exit+Data"', source)
        self.assertIn('"Badge · optional"', source)
        self.assertIn('"Stale · optional"', source)


class AdapterTests(unittest.TestCase):
    def _temporary_root(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        return root

    def args(self, **updates):
        values = {"backend":"managed-http","mode":"manual","variant":None,"seconds":2,"run_id":"host-test","kit":"kit","port":None,"no_build":False}
        values.update(updates)
        return SimpleNamespace(**values)

    def test_backend_rejected_before_any_host_io_and_variant_phase_mismatch(self):
        host = Mock()
        spec = get_lab("lab06")
        args = self.args(backend="local")
        with self.assertRaises(ProjectConfigError):
            adapter_module.Adapter().run(host, object(), spec, args, mode="manual")
        host.resolve_run_target.assert_not_called()
        with self.assertRaises(ProjectConfigError):
            adapter_module.Adapter().run(host, object(), spec, self.args(variant="scripted"), mode="manual")
        host.resolve_run_target.assert_not_called()

    def test_scripted_variant_selected_by_mode(self):
        spec = get_lab("lab06")
        host = Mock()
        args = self.args(mode="scripted")
        captured = {}
        def runner(**kwargs):
            captured.update(kwargs)
            return 0
        with patch.object(adapter_module,"run_with_adapter",side_effect=runner):
            adapter_module.Adapter().run(host, object(), spec, args, mode="scripted")
        self.assertEqual(captured["variant"], "scripted")
        self.assertEqual(captured["mode"], "scripted")
        self.assertIsNone(captured["hooks"].server)

    def test_serial_log_is_bounded_and_redacts_urls_and_credentials(self):
        safe=adapter_module._safe_line(b"status=ok url=https://example.invalid/a password=secret Bearer abc.def.ghi",parser)
        text=safe.decode()
        self.assertNotIn("https://",text)
        self.assertNotIn("password=secret",text)
        self.assertNotIn("Bearer abc",text)
        json_password=adapter_module._safe_line(b'{"password":"secret"}',parser)
        self.assertNotIn(b"secret",json_password)
        self.assertIn(b'"password":"[REDACTED]"',json_password)
        with self.assertRaises(RuntimeError):
            adapter_module._safe_line(b"x"*(adapter_module.MAX_LINE_BYTES+1),parser)

    def test_capture_rejects_json_sensitive_fields_before_persisting_them(self):
        sensitive_lines = (b'{"pin":"1234"}', b'{"media_payload":"AAAA"}')
        for index, secret_line in enumerate(sensitive_lines):
            with self.subTest(secret_line=secret_line):
                class FakeSerial:
                    def __init__(self,*args,**kwargs):
                        self.buffer=(b"LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1\n"
                                     + secret_line + b"\n")
                    def reset_input_buffer(self): pass
                    def read(self,n): payload,self.buffer=self.buffer,b""; return payload
                    def close(self): pass
                host=SimpleNamespace(ROOT=self._temporary_root())
                target=SimpleNamespace(device="/dev/fake")
                with patch.dict(sys.modules,{"serial":types.SimpleNamespace(Serial=lambda *a,**k:FakeSerial())}):
                    with self.assertRaises(RuntimeError):
                        adapter_module._capture(host,target,f"json-secret-{index}",1,"scripted","scripted",parser)
                persisted=(host.ROOT/"logs/device/labs/lab-06"/f"json-secret-{index}"/"serial.txt").read_bytes()
                self.assertNotIn(b"1234",persisted)
                self.assertNotIn(b"secret",persisted)
                self.assertNotIn(b"AAAA",persisted)

    def test_capture_redacts_json_password_before_persisting_it(self):
        class FakeSerial:
            def __init__(self,*args,**kwargs):
                self.buffer=(b"LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1\n"
                             b'{"password":"secret"}\nLAB06_HARNESS_DONE status=complete\n')
            def reset_input_buffer(self): pass
            def read(self,n): payload,self.buffer=self.buffer,b""; return payload
            def close(self): pass
        host=SimpleNamespace(ROOT=self._temporary_root())
        target=SimpleNamespace(device="/dev/fake")
        with patch.dict(sys.modules,{"serial":types.SimpleNamespace(Serial=lambda *a,**k:FakeSerial())}):
            captured=adapter_module._capture(host,target,"json-password",1,"scripted","scripted",parser)
        persisted=captured.read_bytes()
        self.assertNotIn(b"secret",persisted)
        self.assertIn(b'{"password":"[REDACTED]"}',persisted)

    def test_capture_preserves_nonsecret_json_trace_line(self):
        trace_line=b'{"schema":"lab06-trace/v1","event":"queue_overflow","safe":true}'
        class FakeSerial:
            def __init__(self,*args,**kwargs):
                self.buffer=(b"LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1\n"
                             + trace_line + b"\nLAB06_HARNESS_DONE status=complete\n")
            def reset_input_buffer(self): pass
            def read(self,n): payload,self.buffer=self.buffer,b""; return payload
            def close(self): pass
        host=SimpleNamespace(ROOT=self._temporary_root())
        target=SimpleNamespace(device="/dev/fake")
        with patch.dict(sys.modules,{"serial":types.SimpleNamespace(Serial=lambda *a,**k:FakeSerial())}):
            captured=adapter_module._capture(host,target,"safe-json",1,"scripted","scripted",parser)
        self.assertIn(trace_line,captured.read_bytes())

    def test_capture_discards_pre_reset_marker(self):
        class FakeSerial:
            def __init__(self,*args,**kwargs): self.buffer=b"LAB06_READY build=old mode=scripted server=none trace_schema=lab06-trace/v1\n"; self.after_reset=b""; self.calls=[]
            def reset_input_buffer(self): self.calls.append("reset"); self.buffer=self.after_reset
            def read(self,n): payload,self.buffer=self.buffer,b""; return payload
            def close(self): pass
        fake = FakeSerial()
        serial_module = types.SimpleNamespace(Serial=lambda *a,**k:fake)
        host = SimpleNamespace(ROOT=self._temporary_root())
        target = SimpleNamespace(device="/dev/fake")
        with patch.dict(sys.modules,{"serial":serial_module}), patch.object(adapter_module,"BOOT_TIMEOUT_SECONDS",0):
            with self.assertRaises(RuntimeError):
                adapter_module._capture(host,target,"pre-reset",1,"scripted","scripted",parser)
        self.assertEqual(fake.calls,["reset"])

    def test_capture_accepts_only_matching_post_reset_ready_and_complete_harness(self):
        class FakeSerial:
            def __init__(self,*args,**kwargs): self.buffer=b""; self.calls=[]
            def reset_input_buffer(self):
                self.calls.append("reset")
                self.buffer=(b"LAB06_READY build=lab06-fw-scripted-v3 mode=scripted server=none trace_schema=lab06-trace/v1\n"
                             b"LAB06_HARNESS_DONE status=complete\n")
            def read(self,n): payload,self.buffer=self.buffer,b""; return payload
            def close(self): pass
        fake=FakeSerial()
        host=SimpleNamespace(ROOT=self._temporary_root())
        target=SimpleNamespace(device="/dev/fake")
        with patch.dict(sys.modules,{"serial":types.SimpleNamespace(Serial=lambda *a,**k:fake)}):
            captured=adapter_module._capture(host,target,"post-reset",1,"scripted","scripted",parser)
        self.assertEqual(fake.calls,["reset"])
        self.assertIn("LAB06_HARNESS_DONE status=complete",captured.read_text())
        captured.unlink(); captured.parent.rmdir(); captured.parent.parent.rmdir()

        class MismatchSerial(FakeSerial):
            def reset_input_buffer(self):
                self.calls.append("reset")
                self.buffer=b"LAB06_READY build=lab06-fw-scripted-v3 mode=manual server=none trace_schema=lab06-trace/v1\n"
        mismatched=MismatchSerial()
        host=SimpleNamespace(ROOT=self._temporary_root())
        with patch.dict(sys.modules,{"serial":types.SimpleNamespace(Serial=lambda *a,**k:mismatched)}):
            with self.assertRaises(RuntimeError):
                adapter_module._capture(host,target,"mismatch",1,"scripted","scripted",parser)
        self.assertEqual(mismatched.calls,["reset"])

    def test_failed_cleanup_overrides_experiment_and_workflow_success(self):
        spec = get_lab("lab06")
        fake_parser = SimpleNamespace(DECIDED=parser.DECIDED,parse_file=lambda *a,**k:{"lab_metrics":{"experiment_verdict":"PASS"}})
        target = SimpleNamespace(kit_id="kit",device="/dev/fake")
        host = SimpleNamespace(ROOT=ROOT,_flash_from_env_and_args=lambda *a,**k:False,
            resolve_run_target=lambda *a,**k:target,resolve_variant=lambda *a,**k:spec.variants["manual"],
            build_lab=lambda *a,**k:SimpleNamespace(),revalidate_run_target=lambda *a,**k:None,flash_lab_build=lambda *a,**k:None)
        class Evidence:
            def update_context(self,*a,**k): pass
            def event_sink(self,*a,**k): pass
        evidence=Evidence()
        evidence.run_dir = self._temporary_root()
        def runner(**kwargs):
            return kwargs["execute"](SimpleNamespace(run_id="host-test"),evidence)
        fake_serial = ROOT/".tmp-lab06-test-serial"
        fake_serial.write_text("serial")
        try:
            with patch.object(adapter_module,"_parser_module",return_value=fake_parser), patch.object(adapter_module,"_capture",return_value=fake_serial), patch.object(adapter_module,"_build_identity",return_value={"build_id":"lab06-fw-manual-v3","build_fingerprint":"sha256:test","source_sha256":"s","fixture_sha256":"f","sdkconfig_sha256":"c","binary_sha256":"b"}), patch.object(adapter_module,"_tree_hash",side_effect=["s","f","s","f"]), patch.object(adapter_module,"_cleanup",side_effect=RuntimeError("cleanup")), patch.object(adapter_module,"run_with_adapter",side_effect=runner):
                result=adapter_module.Adapter().run(host,object(),spec,self.args(),mode="manual")
            self.assertEqual(result["workflow_status"],"FAIL")
            self.assertEqual(result["experiment_verdict"],"INCOMPLETE")
            self.assertFalse(result["caller_completed"])
            self.assertFalse(result["cleanup_succeeded"])
        finally:
            fake_serial.unlink(missing_ok=True)

    def test_missing_ready_marker_finalizes_partial_run_as_fail_and_unmeasured(self):
        spec=get_lab("lab06")
        fake_parser=SimpleNamespace(DECIDED=parser.DECIDED)
        target=SimpleNamespace(kit_id="kit",device="/dev/fake")
        host=SimpleNamespace(ROOT=ROOT,_flash_from_env_and_args=lambda *a,**k:False,
            resolve_run_target=lambda *a,**k:target,resolve_variant=lambda *a,**k:spec.variants["manual"],
            build_lab=lambda *a,**k:SimpleNamespace())
        evidence=SimpleNamespace(run_dir=self._temporary_root(),update_context=Mock(),event_sink=Mock())
        def runner(**kwargs):
            return kwargs["execute"](SimpleNamespace(run_id="partial-run"),evidence)
        with patch.object(adapter_module,"_parser_module",return_value=fake_parser), \
             patch.object(adapter_module,"_tree_hash",side_effect=["s","f"]), \
             patch.object(adapter_module,"_build_identity",return_value={"build_id":"lab06-fw-manual-v3","build_fingerprint":"sha256:test","source_sha256":"s","fixture_sha256":"f"}), \
             patch.object(adapter_module,"_capture",side_effect=RuntimeError("LAB06_READY missing after reset")), \
             patch.object(adapter_module,"run_with_adapter",side_effect=runner):
            result=adapter_module.Adapter().run(host,object(),spec,self.args(),mode="manual")
        self.assertEqual(result["workflow_status"],"FAIL")
        self.assertEqual(result["experiment_verdict"],"UNMEASURED")
        self.assertFalse(result["caller_completed"])
        self.assertTrue(result["cleanup_succeeded"])
        self.assertTrue(any(call.args and "artifacts" in call.args[0] for call in evidence.update_context.call_args_list))


if __name__ == "__main__":
    unittest.main()
