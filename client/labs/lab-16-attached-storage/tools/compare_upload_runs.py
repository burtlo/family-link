#!/usr/bin/env python3
"""Compare two LAB-16 upload-variant metrics.json runs for repeatability."""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

EXPECTED_PARSER = "lab16-upload-evidence-parser/v3"
MEMORY_CAPTURE_VARIANTS = frozenset({
    "upload",
    "server-offline-during-record",
    "overlap-record-during-upload",
    "live-stream-failover-during-record",
})
# Memory campaign: identity + heap/stack gates only. Timing is captured and reported as informational.
REQUEST_LATENCY_STATS = (
    ("mean", statistics.mean),
    ("median", statistics.median),
    ("max", max),
)

REGIONS = (
    ("internal", "internal_min_free", 1024, 0.05),
    ("internal", "internal_min_largest", 1024, 0.05),
    ("dma", "dma_min_free", 1024, 0.05),
    ("dma", "dma_min_largest", 1024, 0.05),
    ("psram", "psram_min_free", 16 * 1024, 0.005),
    ("psram", "psram_min_largest", 16 * 1024, 0.005),
)
STACK_TASKS = (
    "main_task",
    "sampler_task",
    "event_loop_task",
    "wifi_task",
    "tcpip_task",
)
WINDOW_MAX_FIELDS = (
    "pcm_bytes_max",
    "sd_driver_writes_max",
    "outstanding_buffer_count_max",
    "outstanding_buffer_bytes_max",
    "request_inflight_max",
    "accepted_requests_max",
    "failed_requests_max",
    "retries_max",
)
IDENTITY_KEYS = (
    "expected_chunks",
    "expected_pcm_bytes",
    "recorded_pcm_bytes",
    "parser",
    "variant",
)


@dataclass
class CompareReport:
    run_a: str
    run_b: str
    variant: str = ""
    identity_failures: list[str] = field(default_factory=list)
    memory_failures: list[str] = field(default_factory=list)
    timing_failures: list[str] = field(default_factory=list)
    informational: list[str] = field(default_factory=list)
    memory_checks_passed: int = 0
    memory_checks_total: int = 0

    @property
    def repeatability_pass(self) -> bool:
        return not self.identity_failures and not self.memory_failures


def _tol_bytes(mean: float, floor: float, pct: float) -> float:
    return max(floor, abs(mean) * pct)


def _tol_ms(mean_ms: float) -> float:
    return max(250.0, abs(mean_ms) * 0.10)


def _require_upload_metrics(metrics: dict[str, Any], run_id: str, *, expected_variant: str | None = None) -> None:
    if metrics.get("parser") != EXPECTED_PARSER:
        raise ValueError(f"{run_id}: expected parser {EXPECTED_PARSER}, got {metrics.get('parser')}")
    variant = metrics.get("variant")
    if variant not in MEMORY_CAPTURE_VARIANTS:
        raise ValueError(f"{run_id}: unsupported variant {variant}")
    if expected_variant is not None and variant != expected_variant:
        raise ValueError(f"{run_id}: expected variant {expected_variant}, got {variant}")
    if metrics.get("memory_status") != "COMPLETE":
        raise ValueError(f"{run_id}: memory_status must be COMPLETE for pair comparison")
    if metrics.get("functional_status") != "PASS":
        raise ValueError(f"{run_id}: functional_status must be PASS for pair comparison")
    if not metrics.get("beat_windows"):
        raise ValueError(f"{run_id}: missing beat_windows")


def compare_upload_metrics(metrics_a: dict[str, Any], metrics_b: dict[str, Any],
                           *, run_a: str = "A", run_b: str = "B") -> CompareReport:
    _require_upload_metrics(metrics_a, run_a)
    _require_upload_metrics(metrics_b, run_b)
    variant = metrics_a.get("variant")
    if metrics_b.get("variant") != variant:
        raise ValueError(f"variant mismatch: {metrics_a.get('variant')} vs {metrics_b.get('variant')}")
    report = CompareReport(run_a=run_a, run_b=run_b, variant=str(variant or ""))
    beats_a = metrics_a.get("observed_beats") or []
    beats_b = metrics_b.get("observed_beats") or []
    if beats_a != beats_b:
        report.identity_failures.append(f"observed_beats differ: {beats_a} vs {beats_b}")

    for key in IDENTITY_KEYS:
        va, vb = metrics_a.get(key), metrics_b.get(key)
        if va != vb:
            report.identity_failures.append(f"{key}: {va} vs {vb}")

    for label, field_name in (
        ("device_chunk_source_count", "device_chunk_source_count"),
        ("device_chunk_terminal_count", "device_chunk_terminal_count"),
        ("server_chunk_count", "server_chunk_count"),
    ):
        va, vb = metrics_a.get(field_name), metrics_b.get(field_name)
        if va != vb:
            report.identity_failures.append(f"{label}: {va} vs {vb}")

    windows_a = metrics_a["beat_windows"]
    windows_b = metrics_b["beat_windows"]
    if set(windows_a) != set(windows_b):
        report.identity_failures.append("beat_windows keys differ between runs")

    for beat in beats_a:
        if beat not in windows_a or beat not in windows_b:
            continue
        w0, w1 = windows_a[beat], windows_b[beat]
        for _region, field_name, floor, pct in REGIONS:
            report.memory_checks_total += 1
            a, b = int(w0[field_name]), int(w1[field_name])
            tol = _tol_bytes((a + b) / 2, floor, pct)
            if abs(a - b) > tol:
                report.memory_failures.append(
                    f"{beat} {field_name}: {a} vs {b} (delta {abs(a - b)}, tol {tol:.0f})"
                )
            else:
                report.memory_checks_passed += 1
        for task in STACK_TASKS:
            report.memory_checks_total += 1
            units_a = int(w0[f"{task}_stack_hwm_sdk_units"])
            units_b = int(w1[f"{task}_stack_hwm_sdk_units"])
            bytes_a = units_a * int(w0[f"{task}_stack_type_bytes"])
            bytes_b = units_b * int(w1[f"{task}_stack_type_bytes"])
            tol = _tol_bytes((bytes_a + bytes_b) / 2, 1024, 0.05)
            if abs(bytes_a - bytes_b) > tol:
                report.memory_failures.append(
                    f"{beat} {task}_stack_hwm_bytes: {bytes_a} vs {bytes_b} "
                    f"(delta {abs(bytes_a - bytes_b)}, tol {tol:.0f})"
                )
            else:
                report.memory_checks_passed += 1
        for field_name in WINDOW_MAX_FIELDS:
            va, vb = w0.get(field_name), w1.get(field_name)
            if va != vb:
                report.informational.append(f"{beat} {field_name}: {va} vs {vb} (beat-window max; finals may still match)")

    e0 = int(metrics_a.get("stream_elapsed_ms") or 0)
    e1 = int(metrics_b.get("stream_elapsed_ms") or 0)
    if e0 and e1:
        tol = _tol_ms((e0 + e1) / 2)
        delta = abs(e0 - e1)
        line = f"stream_elapsed_ms: {e0} vs {e1} (delta {delta}, tol {tol:.0f} ms)"
        if delta > tol:
            report.timing_failures.append(line)
        else:
            report.informational.append(line)

    lat0 = metrics_a.get("request_latencies_us") or []
    lat1 = metrics_b.get("request_latencies_us") or []
    if len(lat0) != len(lat1):
        report.identity_failures.append(f"request_latencies_us count: {len(lat0)} vs {len(lat1)}")
    elif lat0:
        for stat_name, pick in REQUEST_LATENCY_STATS:
            v0, v1 = pick(lat0), pick(lat1)
            mean_ms = (v0 + v1) / 2000.0
            tol = _tol_ms(mean_ms)
            delta_ms = abs(v0 - v1) / 1000.0
            line = (
                f"request_latency_{stat_name}_ms: {v0/1000:.1f} vs {v1/1000:.1f} "
                f"(delta {delta_ms:.1f}, tol {tol:.1f} ms)"
            )
            if delta_ms > tol:
                report.timing_failures.append(line)
            else:
                report.informational.append(line)

    return report


def format_report(report: CompareReport) -> str:
    lines = [
        f"LAB-16 memory repeatability ({report.variant or 'unknown'}): {report.run_a} vs {report.run_b}",
        f"Memory/stack checks: {report.memory_checks_passed}/{report.memory_checks_total} within tolerance",
        f"Repeatability: {'PASS' if report.repeatability_pass else 'FAIL'}",
    ]
    if report.identity_failures:
        lines.append("")
        lines.append("Identity (exact match required):")
        lines.extend(f"  - {item}" for item in report.identity_failures)
    if report.memory_failures:
        lines.append("")
        lines.append("Memory / stack (out of tolerance):")
        lines.extend(f"  - {item}" for item in report.memory_failures)
    if report.timing_failures:
        lines.append("")
        lines.append("Timing / networking (informational; does not fail memory repeatability):")
        lines.extend(f"  - {item}" for item in report.timing_failures)
    if report.informational:
        lines.append("")
        lines.append("Informational (does not fail memory repeatability):")
        lines.extend(f"  - {item}" for item in report.informational)
    return "\n".join(lines) + "\n"


def resolve_metrics_path(repo_root: Path, evidence_slug: str, run_id: str,
                         logs_root: Path | None = None) -> Path:
    candidates = [
        repo_root / "docs/labs/evidence" / evidence_slug / "runs" / run_id / "metrics.json",
    ]
    if logs_root is not None:
        candidates.append(logs_root / "labs" / evidence_slug / run_id / "metrics.json")
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError(
        f"metrics.json not found for run {run_id} under evidence or logs (slug={evidence_slug})"
    )


def load_metrics(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def compare_run_files(path_a: Path, path_b: Path, *, run_a: str, run_b: str) -> CompareReport:
    return compare_upload_metrics(load_metrics(path_a), load_metrics(path_b), run_a=run_a, run_b=run_b)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_a", help="First run_id or path to metrics.json")
    parser.add_argument("run_b", help="Second run_id or path to metrics.json")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[4])
    parser.add_argument("--evidence-slug", default="lab-16")
    parser.add_argument("--logs-root", type=Path, default=None,
                        help="Firmware logs root (default: <repo>/logs/device)")
    args = parser.parse_args(argv)

    logs_root = args.logs_root
    if logs_root is None:
        logs_root = args.repo_root / "logs" / "device"

    def resolve_arg(value: str) -> tuple[Path, str]:
        path = Path(value)
        if path.is_file():
            return path.resolve(), path.stem if path.name == "metrics.json" else path.parent.name
        resolved = resolve_metrics_path(args.repo_root, args.evidence_slug, value, logs_root)
        return resolved, value

    path_a, id_a = resolve_arg(args.run_a)
    path_b, id_b = resolve_arg(args.run_b)
    report = compare_run_files(path_a, path_b, run_a=id_a, run_b=id_b)
    print(format_report(report), end="")
    return 0 if report.repeatability_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
