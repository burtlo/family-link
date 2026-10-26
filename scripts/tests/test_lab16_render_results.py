"""LAB-16 memory results markdown renderer."""
from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RENDER = ROOT / "client/labs/lab-16-attached-storage/tools/render_memory_results.py"
spec = importlib.util.spec_from_file_location("lab16_render_memory_results", RENDER)
assert spec is not None and spec.loader is not None
render = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = render
spec.loader.exec_module(render)


class Lab16RenderResultsTest(unittest.TestCase):
    def test_upload_summary_matches_manual_pair_baseline(self):
        metrics = render._load_metrics("20261025-upload-manual-01")
        table = render._summary_table(metrics, render.UPLOAD_SUMMARY_ROWS)
        self.assertIn("260.65", table)
        self.assertIn("329.10", table)
        self.assertIn("Baseline before SD, microphone, and Wi-Fi", table)

    def test_render_all_writes_upload_doc(self):
        paths = render.render_all()
        upload_doc = ROOT / "docs/labs/evidence/lab-16/results/upload.md"
        self.assertIn(upload_doc, paths)
        text = upload_doc.read_text(encoding="utf-8")
        self.assertIn("## Campaign summary (accepted pair)", text)
        self.assertIn("Repeatability: PASS", text)


if __name__ == "__main__":
    unittest.main()
