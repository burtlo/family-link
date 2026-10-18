#!/usr/bin/env python3
"""Keep the package metric inventories in one exact ordered contract."""

import json
import re
import unittest
from pathlib import Path


PACKAGE = Path(__file__).resolve().parents[1]


def manifest_inventory():
    manifest = json.loads((PACKAGE / "manifest.lab.json").read_text())
    return [
        field
        for fields in manifest["metric_schema"]["fields"].values()
        for field in fields
    ]


def readme_inventory():
    readme = (PACKAGE / "README.md").read_text()
    metrics = readme.split("## Metrics and evidence", 1)[1]
    metrics = metrics.split("Raw traces, parsed metrics", 1)[0]
    return re.findall(r"(?m)^([a-z][a-z0-9_]*)$", metrics)


class MetricInventoryTest(unittest.TestCase):
    def setUp(self):
        self.manifest = json.loads((PACKAGE / "manifest.lab.json").read_text())
        self.schema = json.loads((PACKAGE / "trace-schema.v1.json").read_text())
        self.manifest_fields = manifest_inventory()

    def test_manifest_has_one_ordered_field_per_metric(self):
        self.assertEqual(len(self.manifest_fields), len(set(self.manifest_fields)))

    def test_readme_matches_manifest_exactly(self):
        self.assertEqual(readme_inventory(), self.manifest_fields)

    def test_trace_schema_extension_matches_manifest_exactly(self):
        self.assertEqual(
            self.schema["x-contract"]["metric_inventory"], self.manifest_fields
        )

    def test_audio_concurrency_required_subset_matches_manifest_order(self):
        fields = self.manifest["metric_schema"]["fields"]
        expected = [
            field
            for group in ("audio", "drawing_and_concurrency", "memory", "cleanup_and_repetition")
            for field in fields[group]
        ]
        actual = self.schema["$defs"]["audio_concurrency"]["properties"]["metrics"][
            "required"
        ]
        self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
