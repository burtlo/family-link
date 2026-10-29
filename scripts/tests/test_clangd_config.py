from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


class ClangdConfigTests(unittest.TestCase):
    def test_clangd_defines_xtensa_for_host_parsing(self):
        text = (REPO / ".clangd").read_text(encoding="utf-8")
        self.assertIn("-D__XTENSA__", text)

    def test_vscode_query_driver_covers_xtensa_toolchain(self):
        text = (REPO / ".vscode" / "settings.json").read_text(encoding="utf-8")
        self.assertIn("xtensa-*-elf-*", text)


if __name__ == "__main__":
    unittest.main()
