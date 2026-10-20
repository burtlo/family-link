"""Focused native-model checks for the LAB-04 production domain component."""
import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
LAB = ROOT / "client/labs/lab-04-sketch-format"


class Lab04ModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="lab04-model-")
        cls.binary = pathlib.Path(cls.temp.name) / "model_test"
        subprocess.run([
            "c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-I", str(LAB / "components/lab04_format/include"),
            "-I", str(LAB / "main"),
            str(LAB / "components/lab04_format/model.cpp"),
            str(LAB / "tests/model_test.cpp"), "-o", str(cls.binary),
        ], check=True, cwd=ROOT)
        cls.output_dir = pathlib.Path(cls.temp.name) / "variant-output"
        cls.output_dir.mkdir()
        subprocess.run([str(cls.binary), str(LAB / "fixtures/corpus-b.csv"),
                        str(LAB / "fixtures/dense.sketch"), str(cls.output_dir)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_lab04_roundtrip_and_fault_contracts(self):
        subprocess.run([str(self.binary), str(LAB / "fixtures/corpus-b.csv"),
                        str(LAB / "fixtures/dense.sketch")], check=True)

    def test_lab04_preview_bytes(self):
        manifest = json.loads((LAB / "fixtures/manifest.json").read_text())
        self.assertEqual(manifest["preview_storage_bytes"], 28800)
        self.assertLessEqual(manifest["preview_storage_bytes"], 28800)

    def test_lab04_no_silent_truncate(self):
        # The native model test passes a >maximum_input_size payload and checks failure.
        self.test_lab04_roundtrip_and_fault_contracts()

    def test_lab04_cancel(self):
        # The native model test asserts one cancelled terminal and zero live buffers.
        self.test_lab04_roundtrip_and_fault_contracts()

    def test_lab04_failure_publication_is_atomic_and_sticky(self):
        binary = pathlib.Path(self.temp.name) / "failure_publication_test"
        subprocess.run([
            "c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pedantic", "-pthread",
            "-I", str(LAB / "main"), str(LAB / "tests/failure_publication_test.cpp"),
            "-o", str(binary),
        ], check=True, cwd=ROOT)
        subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_app_queue_callbacks_are_installed_after_resources_and_removed_before_cleanup(self):
        source = (LAB / "main/app_main.cpp").read_text()
        app_main = source[source.index('extern "C" void app_main(void)'):]
        queue_check = app_main.index("if (s_jobs == nullptr || s_publications == nullptr)")
        queue_failure_cleanup = app_main.index("cleanup_resources();", queue_check)
        control_exposure = app_main.index("add_button(screen")
        self.assertLess(app_main.index("xQueueCreate(kWorkerQueueCapacity"), queue_check)
        self.assertLess(queue_failure_cleanup, control_exposure)
        self.assertLess(app_main.index('xTaskCreate(worker'), app_main.index("add_button(screen"))
        stopping = app_main.index("if ((s_scenarios_complete || s_harness_failure.failed())")
        self.assertLess(app_main.index("while (!disable_input_callbacks())", stopping),
                        app_main.index("xQueueSend(s_jobs, &stop, 0)", stopping))
        stopped = app_main.index("if (s_stop_requested && ulTaskNotifyTake")
        self.assertLess(stopped, app_main.index("cleanup_resources();", stopped))
        self.assertIn("lv_obj_remove_event_cb(s_controls[i], submit_variant)", source)
        self.assertIn("lv_obj_remove_event_cb(s_controls[i], submit_faults)", source)
        self.assertIn("The BSP display lock is the same recursive mutex held around lv_timer_handler", source)

    def test_queue_full_callback_defers_evidence_io_to_app_task(self):
        source = (LAB / "main/app_main.cpp").read_text()
        callbacks = source[source.index("void submit_variant(lv_event_t *event)", source.index("void worker(")):source.index("bool add_button(")]
        self.assertNotIn("printf", callbacks)
        self.assertNotIn("puts", callbacks)
        self.assertIn("publish_queue_overflow()", callbacks)
        app_main = source[source.index('extern "C" void app_main(void)'):]
        self.assertIn("s_harness_failure.queue_overflows()", app_main)
        self.assertIn("event=worker_queue_overflow", app_main)
        self.assertIn("LAB04_HARNESS_DONE status=failed", app_main)

    def test_allocation_failure_is_a_single_failed_worker_operation(self):
        source = (LAB / "main/app_main.cpp").read_text()
        worker_start = source.index("void worker(void *)")
        worker = source[worker_start:source.index("void submit_variant(", worker_start)]
        self.assertIn("catch (const std::bad_alloc&)", worker)
        self.assertIn('std::strcpy(failed.terminal, "failure")', worker)
        self.assertIn('std::strcpy(failed.reason, "no_memory")', worker)
        self.assertIn("failed.terminal_count = 1", worker)
        app_main = source[source.index('extern "C" void app_main(void)'):]
        self.assertIn("observe_completion(event);", app_main)
        self.assertIn('? "Stopped - reset to rerun"', app_main)
        self.assertIn("LAB04_HARNESS_DONE status=failed", app_main)

    def test_lab04_corpus_hashes(self):
        manifest = json.loads((LAB / "fixtures/manifest.json").read_text())
        source = json.loads((ROOT / "server/labs/lab-03-media/fixtures/manifest.json").read_text())
        for name in ("short.sketch", "dense.sketch"):
            data = (LAB / "fixtures" / name).read_bytes()
            declared = manifest["corpus_a"][name]
            self.assertEqual(len(data), source["assets"][name]["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), source["assets"][name]["sha256"])
            self.assertEqual(declared["bytes"], len(data))
            self.assertEqual(declared["sha256"], hashlib.sha256(data).hexdigest())

    def test_fixture_variant_hashes(self):
        manifest = json.loads((LAB / "fixtures/manifest.json").read_text())
        for name, record in manifest["corpus_b"]["variants"].items():
            data = (self.output_dir / record["file"]).read_bytes()
            self.assertEqual(len(data), record["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), record["sha256"])

    def test_manifest_trace_and_scenario_contract(self):
        project = json.loads((LAB / "manifest.lab.json").read_text())
        for key in ("document", "lab_id", "lab_revision", "firmware_path", "server_path", "fixture_schema", "fixture_revision", "protocol", "trace_schema"):
            self.assertIn(key, project)
        self.assertEqual(project["document"], "family-link.lab-manifest/v1")
        self.assertEqual(project["firmware_path"], "client/labs/lab-04-sketch-format")
        self.assertEqual(project["server_path"], "none")
        self.assertEqual(project["fixture_schema"], "lab04-sketch-format-fixtures/v1")
        self.assertEqual(project["fixture_revision"], "lab04-sketch-format/v1")
        self.assertEqual(project["protocol"], "local-only")
        self.assertEqual(project["trace_schema"], "lab04-trace/v1")
        trace = json.loads((LAB / "trace-schema.json").read_text())
        self.assertEqual(set(trace["required"]), {"schema", "event"})
        self.assertEqual(set(trace["properties"]["event"]["enum"]), {
            "scenario_terminal", "resource_sample", "resource_peak", "publication_loss", "trace_summary"
        })

        event_schemas = {
            branch["if"]["properties"]["event"]["const"]: branch["then"]
            for branch in trace["allOf"]
        }
        scenario_terminal_required = set(event_schemas["scenario_terminal"]["required"])
        self.assertTrue({
            "scenario", "fixture", "class", "variant", "operation_id",
            "terminal", "terminal_count",
        }.issubset(scenario_terminal_required))
        resource_sample = event_schemas["resource_sample"]
        self.assertTrue({
            "seq", "monotonic_us", "scenario", "fixture", "variant",
            "operation_id", "sample_kind", "phase", "image_resident",
            "image_storage", "image_bytes", "decoded_resident", "decoded_segments",
            "internal_largest_free_block_bytes", "psram_largest_free_block_bytes",
        }.issubset(set(resource_sample["required"])))
        self.assertEqual(resource_sample["properties"]["scenario"]["const"], "F-preview")
        self.assertEqual(resource_sample["properties"]["variant"]["const"], "preview")
        publication_loss = event_schemas["publication_loss"]
        self.assertEqual(set(publication_loss["required"]), {"source", "dropped_events"})
        self.assertEqual(publication_loss["properties"]["source"]["const"],
                         "firmware_publication_queue")
        resource_peak = event_schemas["resource_peak"]
        self.assertEqual(set(resource_peak["required"]), {
            "seq", "monotonic_us", "scenario", "fixture", "variant", "operation_id",
            "sample_kind", "phase", "image_resident", "image_storage", "image_bytes",
            "decoded_resident", "expected_output_segments", "decoded_capacity_bytes",
            "internal_largest_free_block_bytes", "psram_largest_free_block_bytes",
            "ledger_b_image_resident", "ledger_b_image_storage", "ledger_b_image_bytes",
            "ledger_b_image_caps", "ledger_b_image_lifetime", "ledger_b_decoded_resident",
            "ledger_b_expected_output_segments", "ledger_b_decoded_capacity_bytes",
        })
        self.assertEqual(resource_peak["properties"]["scenario"]["const"], "F-preview")
        self.assertEqual(resource_peak["properties"]["fixture"]["enum"], ["corpus-a-short", "corpus-b"])
        self.assertEqual(resource_peak["properties"]["variant"]["const"], "preview")
        self.assertEqual(resource_peak["properties"]["sample_kind"]["const"], "peak")
        self.assertEqual(resource_peak["properties"]["phase"]["const"],
                         "maximum_output_capacity_reserved_before_record_insertion_encoded_payload_live")
        self.assertEqual(resource_peak["properties"]["image_bytes"]["const"], 28800)
        self.assertEqual(resource_peak["properties"]["expected_output_segments"]["minimum"], 1)
        self.assertEqual(resource_peak["properties"]["decoded_capacity_bytes"]["minimum"], 1)
        self.assertEqual(set(resource_peak["anyOf"][1]["required"]), {"lvgl_sample_status"})
        trace_summary = event_schemas["trace_summary"]
        self.assertEqual(set(trace_summary["required"]), {
            "expected_rows", "publication_queue_drops", "serial_write_errors"
        })
        self.assertNotIn("seq", trace_summary["required"])
        self.assertTrue({"expected_rows", "publication_queue_drops", "serial_write_errors"}.issubset(
            trace["properties"]))
        # New events remain optional so schema v1 captures from earlier builds
        # keep validating against their original row shapes.
        self.assertEqual(set(trace["required"]), {"schema", "event"})

        raster_fields = {"raster_work_cap", "raster_eligible", "raster_rendered", "raster_remaining"}
        self.assertTrue(raster_fields.isdisjoint(set(trace["required"])))
        self.assertEqual(set(trace["dependentRequired"]), raster_fields)
        self.assertTrue(all(set(required_fields) == raster_fields - {field}
                            for field, required_fields in trace["dependentRequired"].items()))
        self.assertEqual(trace["properties"]["scenario"]["enum"], [
            "F-points", "F-strokes", "F-preview", "F-checkpoint", "F-legacy", "F-malformed", "F-overflow", "F-cancel"
        ])
        self.assertEqual(trace["properties"]["variant"]["enum"][:3], ["points", "strokes", "preview"])
        readme = (LAB / "README.md").read_text()
        self.assertIn("S-17 is not applicable", readme)
        self.assertIn("S-10 parser sensitive-data and redaction fixture tests are HOST-owned", readme)
        self.assertIn("required in the HOST package", readme)
        self.assertIn("provisional lab probe", readme)
        self.assertIn("not a full-raster duration", readme)
        self.assertIn("`raster_remaining`", readme)
        self.assertNotIn("selected_format", readme)
        self.assertNotIn("selected_format", json.dumps(project))
        for scenario in ("F-points", "F-strokes", "F-preview", "F-checkpoint", "F-legacy"):
            self.assertIn(f"`{scenario}` | exploratory", readme)
        self.assertIn("`F-checkpoint` | exploratory | explicit `unsupported` for every encoding", readme)
        for scenario, terminal in (("F-malformed", "failure"), ("F-overflow", "failure"), ("F-cancel", "cancelled")):
            line = next(row for row in readme.splitlines() if f"`{scenario}` | decided" in row)
            self.assertIn(terminal, line)


if __name__ == "__main__":
    unittest.main()
