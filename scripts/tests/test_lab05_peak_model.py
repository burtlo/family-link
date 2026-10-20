"""Compile and run the LAB-05 peak production C model."""

import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-05-transition-proof"
MODEL = LAB / "components/lab05_peak/lab05_peak_model.c"
HEADER = LAB / "components/lab05_peak/include"
TEST_C = LAB / "tests/test_lab05_peak_native.c"

BUDGET_KEYS = (
    "max_live_nodes",
    "max_transient_bytes",
    "max_persistent_bytes",
    "max_concurrent_operations",
    "max_stack_or_call_depth",
    "maximum_input_size",
    "steady_live_images",
)


def fill_image(pattern):
    out = bytearray(28800)
    for index in range(0, 28800, 2):
        if pattern == 0:
            pixel = 0x001F
        else:
            pixel = ((index // 2) & 0x3F) << 5
        out[index] = pixel & 0xFF
        out[index + 1] = (pixel >> 8) & 0xFF
    return bytes(out)


def parse_budget(text):
    fields = {}
    in_block = False
    for line in text.splitlines():
        if line.strip() == "ResourceBudget":
            in_block = True
            continue
        if not in_block:
            continue
        if not line.startswith("  ") or ":" not in line:
            if fields:
                break
            continue
        key, value = line.split(":", 1)
        fields[key.strip()] = int(value.split("#", 1)[0].strip())
    return fields


def _c_function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(signature)


def parse_budget_line(stdout):
    for line in stdout.splitlines():
        if line.startswith("BUDGET "):
            fields = {}
            for part in line.split()[1:]:
                key, value = part.split("=", 1)
                fields[key] = int(value)
            return fields
    raise AssertionError("missing BUDGET line\n" + stdout)


class Lab05PeakModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which("cc") or shutil.which("gcc")
        if not cc:
            raise unittest.SkipTest("host C compiler unavailable")
        cls.temp = tempfile.TemporaryDirectory(prefix="lab05-peak-model-")
        suffix = ".exe" if os.name == "nt" else ""
        cls.binary = pathlib.Path(cls.temp.name) / ("lab05_peak_model_test" + suffix)
        compile_command = [
            cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-DCONFIG_LAB05_PEAK=1",
            "-I", str(HEADER),
            str(TEST_C), str(MODEL),
            "-o", str(cls.binary),
        ]
        subprocess.run(compile_command, check=True, cwd=ROOT)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_model(self, *args):
        result = subprocess.run(
            [str(self.binary), *args],
            check=False, capture_output=True, text=True, cwd=ROOT,
        )
        detail = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, detail)
        return result.stdout

    def test_lab05_manifest_s02_and_l05_02(self):
        project = json.loads((LAB / "manifest.lab.json").read_text(encoding="utf-8"))
        for key in (
            "document", "lab_id", "firmware_path", "server_path", "fixture_schema",
            "fixture_revision", "protocol", "trace_schema",
        ):
            self.assertIn(key, project)
        self.assertEqual(project["document"], "family-link.lab-manifest/v1")
        self.assertEqual(project["lab_id"], "LAB-05")
        self.assertEqual(project["lab_revision"], "v1-proof")
        self.assertEqual(project["firmware_path"], "client/labs/lab-05-transition-proof")
        self.assertEqual(project["server_path"], "none")
        self.assertEqual(project["fixture_schema"], "none")
        self.assertEqual(project["fixture_revision"], "none")
        self.assertEqual(project["protocol"], "local two-state LVGL transition proof")
        self.assertEqual(project["trace_schema"], "lab05-transition/v1")
        self.assertEqual(project["peak"]["lab_revision"], "v1-peak")
        self.assertEqual(project["peak"]["evidence_slug"], "lab-05-peak")
        self.assertEqual(project["peak"]["trace_schema"], "lab05-peak-trace/v1")
        self.assertEqual(project["peak"]["server_path"], "none")

    def test_lab05_peak_handles(self):
        stdout = self.run_model("handles")
        self.assertIn("HANDLES reads_after_delete=0 writes_through_old_handle=0 freed_pixels=1", stdout)

    def test_lab05_peak_alloc_fail(self):
        stdout = self.run_model("alloc-fail")
        self.assertIn(
            "ALLOC_FAIL scenario=P-alloc-fail terminal=failure reason=alloc "
            "terminal_count=1 control=persistent reads_after_delete=0 second_terminal=rejected",
            stdout,
        )

    def test_lab05_peak_stale_handle(self):
        stdout = self.run_model("stale")
        self.assertIn(
            "STALE scenario=P-stale-callback terminal=stale terminal_count=1 "
            "operation_id=50 writes_through_old_handle=0",
            stdout,
        )

    def test_lab05_peak_cancel(self):
        stdout = self.run_model("cancel")
        self.assertIn(
            "CANCEL scenario=P-cancel operation_id=11 terminal=cancelled terminal_count=1 "
            "live_image_count=1 steady_live_images=1",
            stdout,
        )
        self.assertIn(
            "WORKERS audio_terminal=success audio_terminal_count=1 "
            "page_terminal=success page_terminal_count=1",
            stdout,
        )
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertEqual(parse_budget(readme)["steady_live_images"], 1)

    def test_lab05_peak_queue_overflow(self):
        stdout = self.run_model("overflow")
        self.assertIn("queue=audio-stop capacity=8 count=8 overflow_count=2 dropped=0", stdout)
        self.assertIn("first_id=101 last_id=108", stdout)
        self.assertIn("terminal=failure reason=overflow terminal_count=1", stdout)
        self.assertIn("queue=page-apply capacity=8 count=8 overflow_count=1 dropped=0", stdout)
        self.assertIn("OVERFLOW_DRAIN accepted_audio=8 accepted_page=8", stdout)
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("audio-stop queue capacity", readme)
        self.assertIn("page-apply queue capacity", readme)

    def test_lab05_peak_one_terminal(self):
        stdout = self.run_model("terminals")
        self.assertIn(
            "TERMINAL first=success second=rejected terminal_count=1 "
            "outcomes=success,failure,cancelled,stale,timed_out",
            stdout,
        )

    def test_lab05_peak_single_writer(self):
        stdout = self.run_model("writer")
        self.assertIn(
            "WRITER stage_after_worker=cover_down partial_y=120 cleared=0 rejected=1 "
            "session_stage_after=steady",
            stdout,
        )

    def test_lab05_peak_overlap_completeness(self):
        ok = self.run_model("overlap")
        self.assertIn("OVERLAP repetitions=20 completeness=ok missing=0 duplicate_ids=0 extra=0", ok)
        self.assertIn("max_live_images=2 peak_bytes=57600 live_after=1", ok)
        missing = self.run_model("overlap-missing")
        self.assertIn("OVERLAP_MISSING repetitions=19 completeness=fail missing=1", missing)
        duplicate = self.run_model("overlap-duplicate")
        self.assertIn("OVERLAP_DUPLICATE completeness=fail", duplicate)
        self.assertIn("terminal_count=1", duplicate)
        extra = self.run_model("overlap-extra")
        self.assertIn("OVERLAP_EXTRA completeness=fail extra=1", extra)

    def test_lab05_peak_replace(self):
        stdout = self.run_model("replace")
        self.assertIn(
            "REPLACE success_terminal=success failure_terminal=failure failure_reason=alloc "
            "distinct_ids=1 max_live_images=1 reads_after_delete=0",
            stdout,
        )

    def test_lab05_peak_resource_budget(self):
        stdout = self.run_model("budget")
        printed = parse_budget_line(stdout)
        readme = parse_budget((LAB / "README.md").read_text(encoding="utf-8"))
        fixture = json.loads((LAB / "fixtures/resource-budget.json").read_text(encoding="utf-8"))
        self.assertTrue(fixture["provisional_lab_probe"])
        self.assertFalse(fixture["product_budget"])
        for key in BUDGET_KEYS:
            self.assertIn(key, readme)
            self.assertEqual(readme[key], printed[key])
            self.assertEqual(fixture[key], printed[key])
        self.assertIn("provisional lab probe", (LAB / "README.md").read_text(encoding="utf-8").lower())
        self.assertIn("CONCURRENT running=3 fourth=rejected", stdout)
        self.assertIn("INPUT accepted=57600 rejected=57601 terminal=failure reason=input_too_large", stdout)
        self.assertIn("canvas_width=320 canvas_height=240", stdout)
        self.assertIn("cover_ms=400 incoming_y_start=240 incoming_y_end=0", stdout)
        self.assertIn("not a frame budget", (LAB / "README.md").read_text(encoding="utf-8"))

    def test_lab05_peak_fixtures(self):
        page = json.loads((LAB / "fixtures/synthetic-page.json").read_text(encoding="utf-8"))
        self.assertEqual(page["record_count"], 16)
        labels = []
        for index, record in enumerate(page["records"], start=1):
            self.assertEqual(record["summary_id"], index)
            self.assertEqual(record["label"], f"summary-{index:02d}")
            labels.append(record["label"])
        image_a = LAB / "fixtures/image-a.rgb565"
        image_b = LAB / "fixtures/image-b.rgb565"
        stdout = self.run_model("images", str(image_a), str(image_b))
        self.assertIn("IMAGES bytes=28800 pair=57600 match=1 third=rejected", stdout)
        self.assertIn("PAGE count=16 reject17=1 labels=" + ",".join(labels), stdout)
        manifest = json.loads((LAB / "fixtures/manifest.json").read_text(encoding="utf-8"))
        for name, pattern in (("image-a.rgb565", 0), ("image-b.rgb565", 1)):
            data = (LAB / "fixtures" / name).read_bytes()
            self.assertEqual(len(data), 28800)
            self.assertEqual(data, fill_image(pattern))
            declared = manifest["images"][name]
            self.assertEqual(declared["bytes"], 28800)
            self.assertEqual(declared["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(manifest["image_pair_bytes"], 57600)
        self.assertEqual(manifest["page_records"], 16)

    def test_lab05_peak_trace_contract(self):
        schema = json.loads((LAB / "peak-trace-schema.json").read_text(encoding="utf-8"))
        self.assertEqual(schema["schema"], "lab05-peak-trace/v1")
        self.assertEqual(schema["prefix"], "LAB05_PEAK_TRACE")
        self.assertFalse(schema["measured_on_device"])
        for field in (
            "scenario", "control", "terminal", "operation_id", "terminal_count", "live_image_count",
        ):
            self.assertIn(field, schema["required_fields"])
        for field in (
            "lvgl_high_water_bytes", "lvgl_largest_free_bytes", "internal_largest_free_bytes",
            "psram_largest_free_bytes", "anim_frame_us",
        ):
            self.assertIn(field, schema["device_metric_fields"])
        self.assertIn("temp_image_bytes", schema["model_accounting_fields"])
        self.assertIn("kind", schema["model_accounting_fields"])
        stdout = self.run_model("trace")
        self.assertIn("LAB05_PEAK_TRACE schema=lab05-peak-trace/v1", stdout)
        self.assertIn("scenario=P-overlap", stdout)
        self.assertIn("kind=repetition", stdout)
        self.assertIn("control=persistent", stdout)
        self.assertIn("terminal=success", stdout)
        self.assertIn("terminal_count=1", stdout)
        self.assertIn("live_image_count=2", stdout)
        self.assertIn("temp_image_bytes=57600", stdout)
        for field in schema["device_metric_fields"]:
            self.assertIn(field + "=unmeasured", stdout)
        self.assertNotIn("password", stdout)
        self.assertNotIn("Bearer", stdout)
        self.assertNotIn("://", stdout)

    def test_lab05_peak_scenario_inventory(self):
        schema = json.loads((LAB / "peak-trace-schema.json").read_text(encoding="utf-8"))
        stdout = self.run_model("contract")
        self.assertIn("EXPERIMENT_INPUTS P-alloc-fail,P-stale-callback,P-cancel", stdout)
        self.assertIn("EXPLORATORY P-overlap,P-replace", stdout)
        self.assertEqual(schema["experiment_inputs"], ["P-alloc-fail", "P-stale-callback", "P-cancel"])
        classes = {item["id"]: item for item in schema["scenarios"]}
        self.assertEqual(classes["P-overlap"]["class"], "exploratory")
        self.assertFalse(classes["P-overlap"]["experiment_input"])
        self.assertEqual(classes["P-replace"]["class"], "exploratory")
        self.assertFalse(classes["P-replace"]["experiment_input"])
        self.assertTrue(classes["P-alloc-fail"]["experiment_input"])
        self.assertTrue(classes["P-stale-callback"]["experiment_input"])
        self.assertTrue(classes["P-cancel"]["experiment_input"])
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("does not write `experiment_verdict`", readme)

    def test_lab05_peak_sources_excluded_when_unset(self):
        cmake = (LAB / "components/lab05_peak/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("if(CONFIG_LAB05_PEAK)", cmake)
        if_branch, else_branch = cmake.split("else()", 1)
        self.assertIn('SRCS "lab05_peak_model.c"', if_branch)
        self.assertNotIn("lab05_peak_model.c", else_branch)
        self.assertNotIn(".c", else_branch)
        kconfig = (LAB / "components/lab05_peak/Kconfig").read_text(encoding="utf-8")
        self.assertIn("config LAB05_PEAK", kconfig)
        self.assertIn("default n", kconfig)
        self.assertIn("depends on LAB05_PEAK", kconfig)
        for name in ("sdkconfig.defaults", "sdkconfig.defaults.scripted"):
            text = (LAB / name).read_text(encoding="utf-8")
            self.assertNotIn("CONFIG_LAB05_PEAK", text)
        model = MODEL.read_text(encoding="utf-8")
        self.assertIn("#if defined(CONFIG_LAB05_PEAK)", model)
        self.assertNotIn("socket.h", model)
        self.assertNotIn("lab02", model.lower())
        self.assertNotIn("winsock", model.lower())

    def test_lifecycle_markers_unchanged(self):
        app = (LAB / "main/app_main.c").read_text(encoding="utf-8")
        self.assertIn("#if CONFIG_LAB05_PEAK", app)
        before, after = app.split("#else", 1)
        self.assertIn("lab05_peak_harness_main", before)
        self.assertNotIn("LAB05_PEAK_READY", before)
        self.assertNotIn("LAB05_PEAK_HARNESS_DONE", before)
        self.assertIn("LAB05_READY", after)
        self.assertIn("LAB05_HARNESS_BEGIN", after)
        self.assertIn("LAB05_HARNESS_DONE status=transitioned", after)
        self.assertNotIn("LAB05_PEAK_READY", after)
        self.assertNotIn("LAB05_PEAK_HARNESS_DONE", after)
        harness = (LAB / "components/lab05_peak/lab05_peak_harness.c").read_text(encoding="utf-8")
        self.assertIn("LAB05_PEAK_READY build=%s mode=%s control=%s server=none\\n", harness)
        self.assertIn("LAB05_PEAK_HARNESS_DONE status=complete control=%s\\n", harness)
        cmake = (LAB / "components/lab05_peak/CMakeLists.txt").read_text(encoding="utf-8")
        if_branch, else_branch = cmake.split("else()", 1)
        self.assertIn("lab05_peak_harness.c", if_branch)
        self.assertNotIn("lab05_peak_harness.c", else_branch)

    def test_lab05_peak_cover_alloc_failure(self):
        stdout = self.run_model("cover-alloc")
        self.assertIn(
            "COVER_ALLOC overlap_terminal=failure overlap_count=1 animation_active=0 "
            "running_operations=0 cancel_terminal=failure cancel_count=1",
            stdout,
        )

    def test_lab05_peak_ledger_reserves_terminal(self):
        stdout = self.run_model("ledger-full")
        self.assertIn(
            "LEDGER_FULL complete=rejected animation_active=1 stage=cover_down "
            "live_images=2 terminal=absent",
            stdout,
        )
        self.assertIn("LEDGER_SLOT complete=accepted terminal=success live_after_release=1", stdout)

    def test_lab05_peak_view_hold_defers_free(self):
        stdout = self.run_model("view-hold")
        self.assertIn(
            "VIEW_HOLD retained_pixels=1 reads_while_held=0 freed_after_release=1 immediate_free=1",
            stdout,
        )

    def test_lab05_peak_heap_provenance_seam(self):
        stdout = self.run_model("heap-provenance")
        self.assertIn("HEAP captured=instrumented uncaptured=unmeasured", stdout)
        harness = (LAB / "components/lab05_peak/lab05_peak_harness.c").read_text(encoding="utf-8")
        self.assertIn("lab05_peak_heap_metrics_are_instrumented", harness)
        self.assertIn("memory_captured", harness)

    def test_page_worker_posts_result_and_does_not_set_label(self):
        harness = (LAB / "components/lab05_peak/lab05_peak_harness.c").read_text(encoding="utf-8")
        body = _c_function_body(harness, "static void page_worker(")
        apply = _c_function_body(harness, "static void apply_pending_page_results(")
        labels = _c_function_body(harness, "static int apply_page_labels_locked(")
        self.assertNotIn("lv_label_set_text", body)
        self.assertNotIn("bsp_display_lock", body)
        self.assertIn("xQueueSend", body)
        self.assertIn("lv_label_set_text", labels)
        self.assertIn("bsp_display_lock", apply)
        self.assertIn("status=requeued", apply)
        self.assertIn("status=rejected", apply)
        cover = _c_function_body(harness, "static int run_cover(")
        waiting = _c_function_body(harness, "static void wait_for_cover(")
        peak = _c_function_body(harness, "static void on_peak(")
        self.assertIn("wait_for_cover", cover)
        self.assertIn("apply_pending_page_results", waiting)
        before_cover = peak.split("run_cover(", 1)[0]
        self.assertIn("LAB05_PEAK_SCENARIO_OVERLAP", before_cover)
        self.assertIn("LAB05_PEAK_SCENARIO_CANCEL", before_cover)

    def test_s10_records_host_exemption_and_scans_fixtures(self):
        readme = (LAB / "README.md").read_text(encoding="utf-8")
        self.assertIn("S-10 parser", readme)
        self.assertIn("HOST-owned", readme)
        banned = ("password=", "Bearer ", "PIN=", "://user:")
        for path in (
            LAB / "fixtures/synthetic-page.json",
            LAB / "fixtures/resource-budget.json",
            LAB / "fixtures/manifest.json",
            LAB / "fixtures/image-a.rgb565",
            LAB / "fixtures/image-b.rgb565",
            LAB / "peak-trace-schema.json",
        ):
            data = path.read_bytes()
            for token in banned:
                self.assertNotIn(token.encode("ascii"), data, path.name)
