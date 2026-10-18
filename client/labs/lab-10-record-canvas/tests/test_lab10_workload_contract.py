#!/usr/bin/env python3
"""Static contract checks for the L10-06 workload metadata boundary."""

import json
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((PACKAGE / "manifest.lab.json").read_text())
SCHEMA = json.loads((PACKAGE / "trace-schema.v1.json").read_text())
README = (PACKAGE / "README.md").read_text()


class WorkloadContractTest(unittest.TestCase):
    def test_manifest_declares_bounded_generator_and_identities(self):
        generator = MANIFEST["workload_generator"]
        self.assertEqual(MANIFEST["package_id"], "L10-06")
        self.assertEqual(MANIFEST["workload_schema"], "lab10-workload/v1")
        self.assertEqual(generator["generator_version"], "synthetic-human-like-v1")
        self.assertEqual(generator["max_events"], 512)
        self.assertEqual(generator["max_accepted_points"], 320)
        self.assertEqual(generator["timestamp_jitter_bound_us"], 2000)
        self.assertFalse(generator["hardware_touch_latency_claim_allowed"])

        workloads = MANIFEST["workloads"]
        manual = workloads["manual"]["stable_identities"]
        synthetic = workloads["deterministic_human_like"]["stable_identities"]
        self.assertEqual(len(manual), len(set(manual)))
        self.assertEqual(len(synthetic), len(set(synthetic)))
        self.assertTrue(all(identity.startswith("manual-") for identity in manual))
        self.assertTrue(all(identity.startswith("synthetic-") for identity in synthetic))
        for identity in manual + synthetic:
            self.assertIn(identity, README)

    def test_trace_schema_requires_workload_metadata(self):
        self.assertIn("workload_metadata", SCHEMA["required"])
        metadata = SCHEMA["$defs"]["workload_metadata"]
        self.assertEqual(metadata["properties"]["schema_version"]["const"],
                         "lab10-workload/v1")
        self.assertIn("seed", metadata["required"])
        self.assertIn("generator_version", metadata["required"])
        self.assertIn("timestamp_jitter_bound_us", metadata["required"])
        self.assertIn("evidence_provenance", metadata["required"])
        self.assertIn("declared_features", metadata["required"])

    def test_trace_event_preserves_coordinate_boundary_and_timing_fields(self):
        event = SCHEMA["$defs"]["event"]["properties"]
        for field in (
            "raw_x",
            "raw_y",
            "display_x",
            "display_y",
            "logical_x",
            "logical_y",
            "stroke_id",
            "color_index",
            "boundary",
            "t_us",
            "ideal_t_us",
            "timestamp_jitter_us",
            "pause_before_us",
            "pen_up_gap_before_us",
        ):
            self.assertIn(field, event)
        contract = SCHEMA["x-contract"]["workload_contract"]
        self.assertEqual(contract["synthetic_provenance"], "host")
        self.assertIn("touch latency", contract["evidence_boundary"])
        self.assertIn("display scanout", contract["evidence_boundary"])


if __name__ == "__main__":
    unittest.main()

