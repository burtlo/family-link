from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[1]
REPO = SCRIPTS.parent
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from normalize_eol import (  # noqa: E402
    CRLF_SUFFIXES,
    ROOT,
    allows_crlf,
    git_ls_files,
    is_binary_path,
)


class RepoEolTests(unittest.TestCase):
    def test_tracked_text_files_use_lf_only(self):
        offenders: list[str] = []
        for rel in git_ls_files():
            if allows_crlf(rel) or is_binary_path(rel):
                continue
            path = ROOT / rel
            if not path.is_file():
                continue
            data = path.read_bytes()
            if b"\r" in data:
                offenders.append(rel.as_posix())
        self.assertEqual(
            [],
            offenders,
            "tracked text files must use LF; run make eol.fix (CRLF allowed only for "
            + ", ".join(sorted(CRLF_SUFFIXES))
            + "; binary suffixes skipped)",
        )

    def test_gitattributes_declares_lf_for_common_source_types(self):
        text = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        for needle in ("*.py text eol=lf", "*.md text eol=lf", "* text=auto eol=lf"):
            self.assertIn(needle, text)


if __name__ == "__main__":
    unittest.main()
