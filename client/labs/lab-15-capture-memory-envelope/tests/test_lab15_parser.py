#!/usr/bin/env python3
"""L15-03/L15-07 parser and experiment input regressions."""
from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE / "tools"))

import parse_lab_trace as parser  # noqa: E402

FIXTURE = PACKAGE / "tests/fixtures/scripted_catalog_serial.txt"


class Lab15ParserTests(unittest.TestCase):
    def test_fixture_produces_lab_metrics_v2(self) -> None:
        serial = FIXTURE.read_text(encoding="utf-8")
        result = parser.parse(serial, run_id="fixture-run", mode="scripted")
        self.assertEqual(result["document"], "family-link.lab-metrics/v2")
        self.assertEqual(result["trace_schema"], "lab15-envelope-trace/v1")
        self.assertEqual(result["experiment_verdict"], "PASS")

    def test_experiment_inputs_exclude_exploratory(self) -> None:
        serial = FIXTURE.read_text(encoding="utf-8")
        result = parser.parse(serial, mode="scripted")
        self.assertEqual(result["experiment_inputs"], list(parser.EXPERIMENT_INPUTS))
        for exploratory in parser.EXPLORATORY:
            self.assertNotIn(exploratory, result["experiment_inputs"])

    def test_metrics_json_round_trip(self) -> None:
        serial = FIXTURE.read_text(encoding="utf-8")
        parsed = parser.parse(serial, mode="scripted")
        payload = json.dumps(parsed)
        self.assertIn("family-link.lab-metrics/v2", payload)


if __name__ == "__main__":
    unittest.main()
