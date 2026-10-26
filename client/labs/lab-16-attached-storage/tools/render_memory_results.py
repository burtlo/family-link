#!/usr/bin/env python3
"""Render LAB-16 memory campaign tables into docs/labs/evidence/lab-16/."""
from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[4]
EVIDENCE_SLUG = "lab-16"
RESULTS_DIR = REPO_ROOT / "docs/labs/evidence" / EVIDENCE_SLUG / "results"
INDEX_PATH = REPO_ROOT / "docs/labs/evidence" / EVIDENCE_SLUG / "README.md"

UPLOAD_SUMMARY_ROWS: tuple[tuple[str, str], ...] = (
    ("baseline_before_sd", "Baseline before SD, microphone, and Wi-Fi"),
    ("sd_mic_recording", "SD + microphone recording active"),
    ("wifi_upload_active", "Wi-Fi and WAV upload active"),
    ("upload_complete_workers_quiescent", "Upload complete, workers quiescent"),
)

UPLOAD_ACCEPTED_PAIR = ("20261025-upload-manual-01", "20261025-upload-manual-02")

VARIANT_STUBS: dict[str, tuple[tuple[str, str], ...]] = {
    "server-offline-during-record": (
        ("baseline", "Baseline before SD, microphone, and Wi-Fi"),
        ("recording", "Recording active with server offline"),
        ("upload", "Server starts and upload is active"),
        ("quiescent", "Upload complete, workers quiescent"),
    ),
    "overlap-record-during-upload": (
        ("baseline", "Baseline before SD, microphone, and Wi-Fi"),
        ("msg1_record", "Message 1 recording"),
        ("overlap", "Message 1 uploading while message 2 records (overlap peak)"),
        ("msg2_upload", "Message 2 uploading"),
        ("quiescent", "Both sessions complete and workers quiescent"),
    ),
    "live-stream-failover-during-record": (
        ("baseline", "Baseline before SD, microphone, and Wi-Fi"),
        ("live", "Live recording and posting"),
        ("fallback", "HTTP 503 / SD fallback active"),
        ("drain", "Server restored / drain active"),
        ("quiescent", "180 chunks accepted and workers quiescent"),
    ),
}


def _import_compare():
    import importlib.util

    path = Path(__file__).with_name("compare_upload_runs.py")
    name = "lab16_compare_upload_runs"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def _kib(value: int | float) -> str:
    return f"{float(value) / 1024.0:.2f}"


def _heap_row(label: str, window: dict[str, Any]) -> str:
    return (
        f"| {label} | {_kib(window['internal_total'])} / {_kib(window['internal_min_free'])} | "
        f"{_kib(window['dma_total'])} / {_kib(window['dma_min_free'])} | "
        f"{_kib(window['dma_min_largest'])} | "
        f"{_kib(window['psram_total'])} / {_kib(window['psram_min_free'])} |"
    )


def _summary_table(metrics: dict[str, Any], rows: tuple[tuple[str, str], ...]) -> str:
    windows = metrics.get("beat_windows") or {}
    header = (
        "| Moment | Internal SRAM heap total / free | "
        "DMA-capable internal SRAM total / free | "
        "Largest free DMA-capable SRAM block | PSRAM heap total / free |\n"
        "|---|---:|---:|---:|---:|\n"
    )
    body: list[str] = []
    for beat_id, label in rows:
        window = windows.get(beat_id)
        if window is None:
            body.append(f"| {label} | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |")
        else:
            body.append(_heap_row(label, window))
    return header + "\n".join(body) + "\n"


def _full_beat_table(metrics: dict[str, Any]) -> str:
    windows = metrics.get("beat_windows") or {}
    beats = metrics.get("observed_beats") or sorted(windows.keys())
    lines = [
        "| Beat | Internal total / min free | DMA total / min free | "
        "DMA min largest | PSRAM total / min free | Samples |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for beat in beats:
        window = windows.get(beat)
        if window is None:
            lines.append(f"| `{beat}` | — | — | — | — | — |")
            continue
        internal = f"{_kib(window['internal_total'])} / {_kib(window['internal_min_free'])}"
        dma = f"{_kib(window['dma_total'])} / {_kib(window['dma_min_free'])}"
        psram = f"{_kib(window['psram_total'])} / {_kib(window['psram_min_free'])}"
        lines.append(
            f"| `{beat}` | {internal} | {dma} | {_kib(window['dma_min_largest'])} | "
            f"{psram} | {window.get('sample_count', '—')} |"
        )
    return "\n".join(lines) + "\n"


def _not_captured_table(rows: tuple[tuple[str, str], ...]) -> str:
    header = (
        "| Moment | Internal SRAM heap total / free | "
        "DMA-capable internal SRAM total / free | "
        "Largest free DMA-capable SRAM block | PSRAM heap total / free |\n"
        "|---|---|---|---|---|\n"
    )
    body = "\n".join(
        f"| {label} | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |"
        for _, label in rows
    )
    return header + body + "\n"


def _load_metrics(run_id: str) -> dict[str, Any]:
    compare = _import_compare()
    path = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_id, REPO_ROOT / "logs" / "device")
    return json.loads(path.read_text(encoding="utf-8"))


def _render_upload(compare) -> str:
    run_a, run_b = UPLOAD_ACCEPTED_PAIR
    metrics_a = _load_metrics(run_a)
    metrics_b = _load_metrics(run_b)
    path_a = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_a, REPO_ROOT / "logs" / "device")
    path_b = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_b, REPO_ROOT / "logs" / "device")
    report = compare.compare_run_files(path_a, path_b, run_a=run_a, run_b=run_b)
    compare_text = compare.format_report(report)

    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    firmware = metrics_a.get("firmware_build_id") or metrics_a.get("build_id") or "(see run manifest)"
    fixture = metrics_a.get("fixture_revision") or "lab16-upload/v2"

    parts = [
        "# LAB-16 VARIANT `upload` — memory results",
        "",
        f"Generated: {generated} by `render_memory_results.py`. "
        "Do not edit by hand; run `make lab.lab16.results`.",
        "",
        "**Accepted repeatability pair:** "
        f"[{run_a}](../runs/{run_a}/run-manifest.json) and "
        f"[{run_b}](../runs/{run_b}/run-manifest.json).",
        "",
        f"**Firmware / fixture:** `{firmware}`, fixture `{fixture}`.",
        "",
        "Column semantics match the campaign README: **total** is the region total at the "
        "beat window; **free** is the **interval minimum free** during that beat; the DMA "
        "block column is the **interval minimum largest free DMA block**. Values are not "
        "comparable to the V2 startup reference (different firmware).",
        "",
        "<a id=\"campaign-summary\"></a>",
        "## Campaign summary (accepted pair)",
        "",
        "Rows below use run A; the pair is identical on these summary beats "
        f"(repeatability **{'PASS' if report.repeatability_pass else 'FAIL'}**).",
        "",
        _summary_table(metrics_a, UPLOAD_SUMMARY_ROWS),
        "<a id=\"repeatability\"></a>",
        "## Repeatability compare",
        "",
        "```text",
        compare_text.rstrip(),
        "```",
        "",
        f"<a id=\"run-{run_a}\"></a>",
        f"## Full beat windows — `{run_a}`",
        "",
        f"Metrics: [metrics.json](../runs/{run_a}/metrics.json)",
        "",
        _full_beat_table(metrics_a),
        "",
        f"<a id=\"run-{run_b}\"></a>",
        f"## Full beat windows — `{run_b}`",
        "",
        f"Metrics: [metrics.json](../runs/{run_b}/metrics.json)",
        "",
        _full_beat_table(metrics_b),
        "",
    ]
    return "\n".join(parts)


def _render_stub(variant: str, title: str) -> str:
    rows = VARIANT_STUBS[variant]
    return "\n".join(
        [
            f"# LAB-16 VARIANT `{variant}` — memory results",
            "",
            "Status: **NOT CAPTURED** — no `metrics.json` with beat windows for this variant yet.",
            "",
            f"Scenario: {title}",
            "",
            "## Campaign summary",
            "",
            _not_captured_table(rows),
        ]
    ) + "\n"


def render_all() -> list[Path]:
    compare = _import_compare()
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []

    upload_path = RESULTS_DIR / "upload.md"
    upload_path.write_text(_render_upload(compare), encoding="utf-8")
    written.append(upload_path)

    stubs = {
        "server-offline-during-record": "server offline during recording",
        "overlap-record-during-upload": "record message 2 while message 1 uploads",
        "live-stream-failover-during-record": "live outage, SD fallback, and drain",
    }
    for variant, title in stubs.items():
        path = RESULTS_DIR / f"{variant}.md"
        path.write_text(_render_stub(variant, title), encoding="utf-8")
        written.append(path)

    index = "\n".join(
        [
            "# LAB-16 evidence — memory campaign results",
            "",
            "Published KiB tables for the [LAB-16 memory campaign]"
            "(../../../client/labs/lab-16-attached-storage/README.md). "
            "Raw artifacts remain under `runs/<run_id>/`; serial captures under "
            "`logs/device/labs/lab-16/`.",
            "",
            "Regenerate tables: `make lab.lab16.results` "
            "(runs `client/labs/lab-16-attached-storage/tools/render_memory_results.py`).",
            "",
            "## VARIANT results",
            "",
            "| VARIANT | Memory status | Published tables |",
            "|---|---|---|",
            "| `upload` | CAPTURED (accepted pair) | [results/upload.md](results/upload.md) |",
            "| `server-offline-during-record` | NOT CAPTURED | "
            "[results/server-offline-during-record.md](results/server-offline-during-record.md) |",
            "| `overlap-record-during-upload` | NOT CAPTURED | "
            "[results/overlap-record-during-upload.md](results/overlap-record-during-upload.md) |",
            "| `live-stream-failover-during-record` | NOT CAPTURED | "
            "[results/live-stream-failover-during-record.md](results/live-stream-failover-during-record.md) |",
            "",
        ]
    )
    INDEX_PATH.write_text(index, encoding="utf-8")
    written.append(INDEX_PATH)
    return written


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.parse_args(argv)
    try:
        paths = render_all()
    except (OSError, json.JSONDecodeError, FileNotFoundError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    for path in paths:
        print(path.relative_to(REPO_ROOT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
