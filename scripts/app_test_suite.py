#!/usr/bin/env python3
"""Discover and run host tests for the v2 product app (client/device + shared libs).

Scope rules live in config/app-test-suite.yaml. See that file for how the suite grows.
"""
from __future__ import annotations

import argparse
import fnmatch
import importlib.util
import re
import sys
import unittest
from pathlib import Path
from typing import Iterable, Sequence

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CONFIG = ROOT / "config" / "app-test-suite.yaml"
TESTS_DIR = ROOT / "scripts" / "tests"
MARKER_INCLUDE = re.compile(r"app-test-suite:\s*include\b")
MARKER_EXCLUDE = re.compile(r"app-test-suite:\s*exclude\b")


def _load_yaml(path: Path) -> dict:
    try:
        import yaml
    except ImportError as exc:  # pragma: no cover
        raise SystemExit("PyYAML missing. Run: make install") from exc
    raw = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise SystemExit(f"Invalid app test suite config: {path}")
    return raw


def _stem_matches_any(stem: str, patterns: Sequence[str]) -> bool:
    return any(fnmatch.fnmatch(stem, pattern) for pattern in patterns)


def _host_module_pattern(modules: Sequence[str]) -> re.Pattern[str]:
    joined = "|".join(re.escape(name) for name in modules)
    return re.compile(rf"(?:^|\n)\s*(?:from|import)\s+(?:{joined})(?:\s|\.|$)")


def discover_stems(config_path: Path = DEFAULT_CONFIG) -> list[str]:
    """Return test module stems (test_foo) under scripts/tests/, stable order."""
    cfg = _load_yaml(config_path)
    source_roots: list[str] = list(cfg.get("source_roots") or ())
    include_globs: list[str] = list(cfg.get("test_include_globs") or ())
    exclude_globs: list[str] = list(cfg.get("test_exclude_globs") or ())
    host_modules: list[str] = list(cfg.get("host_modules") or ())
    include_stems = frozenset(cfg.get("include_stems") or ())
    exclude_stems = frozenset(cfg.get("exclude_stems") or ())
    host_re = _host_module_pattern(host_modules) if host_modules else None

    chosen: list[str] = []
    for path in sorted(TESTS_DIR.glob("test_*.py")):
        stem = path.stem
        if stem in exclude_stems:
            continue
        text = path.read_text(encoding="utf-8")
        if MARKER_EXCLUDE.search(text):
            continue
        if stem in include_stems or MARKER_INCLUDE.search(text):
            chosen.append(stem)
            continue
        if _stem_matches_any(stem, exclude_globs):
            continue
        if _stem_matches_any(stem, include_globs):
            chosen.append(stem)
            continue
        if any(root in text for root in source_roots):
            chosen.append(stem)
            continue
        if host_re and host_re.search(text):
            chosen.append(stem)
            continue
    return chosen


def discover_modules(config_path: Path = DEFAULT_CONFIG) -> list[str]:
    """Return unittest names as scripts.tests.<stem> (for stable listing)."""
    return [f"scripts.tests.{stem}" for stem in discover_stems(config_path)]


def _load_test_module(stem: str):
    path = TESTS_DIR / f"{stem}.py"
    qualname = f"scripts.tests.{stem}"
    spec = importlib.util.spec_from_file_location(qualname, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load test module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[qualname] = module
    spec.loader.exec_module(module)
    return module


def _ensure_import_paths() -> None:
    root = str(ROOT)
    scripts_dir = str(ROOT / "scripts")
    for entry in (root, scripts_dir):
        if entry not in sys.path:
            sys.path.insert(0, entry)


def run_suite(stems: Iterable[str], argv: Sequence[str]) -> int:
    _ensure_import_paths()
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    for stem in stems:
        suite.addTests(loader.loadTestsFromModule(_load_test_module(stem)))
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    return 0 if result.wasSuccessful() else 1


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=Path,
        default=DEFAULT_CONFIG,
        help="Suite scope config (default: config/app-test-suite.yaml)",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="Print discovered unittest module names and exit",
    )
    parser.add_argument(
        "unittest_args",
        nargs=argparse.REMAINDER,
        help="Forwarded to unittest when present (e.g. -k pattern)",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)
    stems = discover_stems(args.config)
    if not stems:
        print("No app test modules discovered.", file=sys.stderr)
        return 2
    if args.list:
        for stem in stems:
            print(f"scripts.tests.{stem}")
        return 0
    extra = [a for a in args.unittest_args if a != "--"]
    if extra:
        return run_suite(stems, extra)
    return run_suite(stems, [])


if __name__ == "__main__":
    raise SystemExit(main())
