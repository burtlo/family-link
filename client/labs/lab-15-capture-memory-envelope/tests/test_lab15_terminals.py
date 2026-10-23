#!/usr/bin/env python3
"""L15-04: one terminal per operation_id."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

PACKAGE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE / "tools"))

import parse_lab_trace as parser  # noqa: E402

FIXTURE = PACKAGE / "tests/fixtures/scripted_catalog_serial.txt"


class Lab15TerminalTests(unittest.TestCase):
    def test_one_terminal_per_operation_id(self) -> None:
        serial = FIXTURE.read_text(encoding="utf-8")
        result = parser.parse(serial, mode="scripted")
        terminals = result["terminals"]
        operation_ids = [row["operation_id"] for row in terminals]
        self.assertEqual(len(operation_ids), len(set(operation_ids)))

    def test_duplicate_terminal_rejected(self) -> None:
        serial = FIXTURE.read_text(encoding="utf-8")
        duplicate = serial.replace(
            "operation_id=1 outcome=duration_cap",
            "operation_id=1 outcome=duration_cap\n"
            "LAB15_TRACE schema=lab15-envelope-trace/v1 event=terminal seq=99 t_us=1 "
            "arm=catalog_pcm_none_none operation_id=1 outcome=cancelled reason=dup",
        )
        with self.assertRaises(parser.ParseError):
            parser.parse(duplicate, mode="scripted")


if __name__ == "__main__":
    unittest.main()
