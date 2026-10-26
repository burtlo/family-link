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

SERVER_OFFLINE_SUMMARY_ROWS: tuple[tuple[str, str], ...] = (
    ("baseline_before_sd", "Baseline before SD, microphone, and Wi-Fi"),
    ("sd_mic_recording", "Recording active with server offline"),
    ("wifi_upload_active", "Server online; Wi-Fi and WAV upload active"),
    ("upload_complete_workers_quiescent", "Upload complete, workers quiescent"),
)

# Set after two repeatable PASS memory runs for VARIANT server-offline-during-record.
SERVER_OFFLINE_ACCEPTED_PAIR: tuple[str, str] = (
    "20261025-server-offline-memory-03",
    "20261025-server-offline-memory-04",
)

OVERLAP_SUMMARY_ROWS: tuple[tuple[str, str], ...] = (
    ("baseline_before_sd", "Baseline before SD, microphone, and Wi-Fi"),
    ("overlap_msg1_recording", "Message 1 recording"),
    ("overlap_concurrent_peak", "Message 1 uploading while message 2 records (overlap peak)"),
    ("overlap_msg2_upload", "Message 2 uploading"),
    ("upload_complete_workers_quiescent", "Both sessions complete and workers quiescent"),
)

OVERLAP_ACCEPTED_PAIR: tuple[str, str] = (
    "20261025-overlap-memory-03",
    "20261025-overlap-memory-05",
)

LIVE_FAILOVER_SUMMARY_ROWS: tuple[tuple[str, str], ...] = (
    ("baseline_before_sd", "Baseline before SD, microphone, and Wi-Fi"),
    ("live_stream_upload_active", "Live recording and HTTP posting"),
    ("live_failover_outage", "Server outage / SD-retained chunks"),
    ("live_sd_drain_active", "Drain of retained chunks after recovery"),
    ("upload_complete_workers_quiescent", "180 chunks accepted and workers quiescent"),
)

# Filled after two repeatable PASS memory runs.
LIVE_FAILOVER_ACCEPTED_PAIR: tuple[str, str] = (
    "20261025-livestream-memory-03",
    "20261025-livestream-memory-04",
)

VARIANT_STUBS: dict[str, tuple[tuple[str, str], ...]] = {
    "server-offline-during-record": SERVER_OFFLINE_SUMMARY_ROWS,
    "overlap-record-during-upload": OVERLAP_SUMMARY_ROWS,
}

# Index table: variant, human title, accepted pair, stress beat for cross-variant snapshot.
VARIANT_FIXTURE_REVISION: dict[str, str] = {
    "upload": "lab16-upload/v2",
    "server-offline-during-record": "lab16-upload/v2",
    "overlap-record-during-upload": "lab16-diary/v1",
    "live-stream-failover-during-record": "lab16-diary/v1",
}

VARIANT_SUMMARY_ROWS: dict[str, tuple[tuple[str, str], ...]] = {
    "upload": UPLOAD_SUMMARY_ROWS,
    "server-offline-during-record": SERVER_OFFLINE_SUMMARY_ROWS,
    "overlap-record-during-upload": OVERLAP_SUMMARY_ROWS,
    "live-stream-failover-during-record": LIVE_FAILOVER_SUMMARY_ROWS,
}

CAMPAIGN_VARIANTS: tuple[tuple[str, str, tuple[str, str], str, str], ...] = (
    (
        "upload",
        "Record to SD, then upload",
        UPLOAD_ACCEPTED_PAIR,
        "wifi_upload_active",
        "Wi-Fi and WAV upload active",
    ),
    (
        "server-offline-during-record",
        "Server offline during recording",
        SERVER_OFFLINE_ACCEPTED_PAIR,
        "sd_mic_recording",
        "Recording with server offline",
    ),
    (
        "overlap-record-during-upload",
        "Overlap record during upload",
        OVERLAP_ACCEPTED_PAIR,
        "overlap_concurrent_peak",
        "Concurrent record + upload peak",
    ),
    (
        "live-stream-failover-during-record",
        "Live stream, outage, SD drain",
        LIVE_FAILOVER_ACCEPTED_PAIR,
        "live_failover_outage",
        "Server outage / SD-retained chunks",
    ),
)


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


def _pct_of_total(part: int | float, total: int | float) -> int:
    total_f = float(total)
    if total_f <= 0:
        return 0
    return int(round(100.0 * float(part) / total_f))


def _min_free_cell(min_free: int | float, total: int | float) -> str:
    return f"{_kib(min_free)} ({_pct_of_total(min_free, total)}%)"


def _dma_block_cell(min_largest: int | float, dma_total: int | float) -> str:
    return f"{_kib(min_largest)} ({_pct_of_total(min_largest, dma_total)}%)"


def _heap_row(label: str, window: dict[str, Any]) -> str:
    return (
        f"| {label} | {_min_free_cell(window['internal_min_free'], window['internal_total'])} | "
        f"{_min_free_cell(window['dma_min_free'], window['dma_total'])} | "
        f"{_dma_block_cell(window['dma_min_largest'], window['dma_total'])} | "
        f"{_min_free_cell(window['psram_min_free'], window['psram_total'])} |"
    )


_GENERIC_MEMORY_HEADER = (
    "| Moment | Internal min free KiB (%) | DMA min free KiB (%) | "
    "Min largest DMA block KiB (%) | PSRAM min free KiB (%) |\n"
    "|---|---:|---:|---:|---:|\n"
)


def _reference_window(
    metrics: dict[str, Any],
    summary_rows: tuple[tuple[str, str], ...] | None = None,
) -> dict[str, Any] | None:
    windows = metrics.get("beat_windows") or {}
    if summary_rows:
        for beat_id, _ in summary_rows:
            candidate = windows.get(beat_id)
            if candidate is not None:
                return candidate
    for beat_id in ("baseline_before_sd",):
        candidate = windows.get(beat_id)
        if candidate is not None:
            return candidate
    if not windows:
        return None
    return windows[next(iter(windows))]


def _memory_table_header(
    ref: dict[str, Any],
    *,
    first_col: str = "Moment",
    trailing_headers: tuple[str, ...] = (),
) -> str:
    internal_total = _kib(ref["internal_total"])
    dma_total = _kib(ref["dma_total"])
    psram_total = _kib(ref["psram_total"])
    cols = [
        first_col,
        f"Internal min free KiB (% of {internal_total} KiB heap total)",
        f"DMA min free KiB (% of {dma_total} KiB DMA heap total)",
        f"Min largest DMA block KiB (% of {dma_total} KiB DMA heap total)",
        f"PSRAM min free KiB (% of {psram_total} KiB heap total)",
        *trailing_headers,
    ]
    align = [":---"] + [":---:"] * (len(cols) - 1)
    header_row = "| " + " | ".join(cols) + " |\n"
    sep_row = "|" + "|".join(align) + "|\n"
    return header_row + sep_row


def _summary_table(metrics: dict[str, Any], rows: tuple[tuple[str, str], ...]) -> str:
    windows = metrics.get("beat_windows") or {}
    ref = _reference_window(metrics, rows)
    header = _memory_table_header(ref) if ref is not None else _GENERIC_MEMORY_HEADER
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
    ref = _reference_window(metrics, None)
    if ref is not None:
        header = _memory_table_header(ref, first_col="Beat", trailing_headers=("Samples",))
        lines = [header.rstrip("\n")]
    else:
        lines = [
            _GENERIC_MEMORY_HEADER.replace("Moment", "Beat").rstrip("\n"),
            "| Samples |",
            "|---:|",
        ]
    for beat in beats:
        window = windows.get(beat)
        if window is None:
            lines.append(f"| `{beat}` | — | — | — | — | — |")
            continue
        lines.append(
            f"| `{beat}` | "
            f"{_min_free_cell(window['internal_min_free'], window['internal_total'])} | "
            f"{_min_free_cell(window['dma_min_free'], window['dma_total'])} | "
            f"{_dma_block_cell(window['dma_min_largest'], window['dma_total'])} | "
            f"{_min_free_cell(window['psram_min_free'], window['psram_total'])} | "
            f"{window.get('sample_count', '—')} |"
        )
    return "\n".join(lines) + "\n"


def _not_captured_table(rows: tuple[tuple[str, str], ...]) -> str:
    header = _GENERIC_MEMORY_HEADER.replace("|---:|---:|---:|---:|", "|---|---|---|---|")
    body = "\n".join(
        f"| {label} | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |"
        for _, label in rows
    )
    return header + body + "\n"


def _load_metrics(run_id: str) -> dict[str, Any]:
    compare = _import_compare()
    path = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_id, REPO_ROOT / "logs" / "device")
    return json.loads(path.read_text(encoding="utf-8"))


def _fixture_revision(metrics: dict[str, Any], default: str) -> str:
    fixture = metrics.get("fixture")
    if isinstance(fixture, dict) and fixture.get("revision"):
        return str(fixture["revision"])
    return metrics.get("fixture_revision") or default


def _render_captured_variant(
    compare,
    *,
    variant: str,
    run_a: str,
    run_b: str,
    summary_rows: tuple[tuple[str, str], ...],
    default_fixture_revision: str,
) -> str:
    metrics_a = _load_metrics(run_a)
    metrics_b = _load_metrics(run_b)
    path_a = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_a, REPO_ROOT / "logs" / "device")
    path_b = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_b, REPO_ROOT / "logs" / "device")
    report = compare.compare_run_files(path_a, path_b, run_a=run_a, run_b=run_b)
    compare_text = compare.format_report(report)

    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    firmware = metrics_a.get("firmware_build_id") or metrics_a.get("build_id") or "(see run manifest)"
    fixture = _fixture_revision(metrics_a, default_fixture_revision)

    parts = [
        f"# LAB-16 VARIANT `{variant}` — memory results",
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
        "Table cells are **interval minimum free** (or minimum largest DMA block) in KiB, "
        "with **percent of that region's heap total** at the beat (totals KiB are in each "
        "column header). Values are not comparable to the V2 startup reference (different firmware).",
        "",
        "**Repeatability:** memory/stack and identity only; timing lines in the compare block "
        "are informational ([TIMING-AND-NETWORK-NOTES.md](../TIMING-AND-NETWORK-NOTES.md)).",
        "",
        "<a id=\"campaign-summary\"></a>",
        "## Campaign summary (accepted pair)",
        "",
        "Rows below use run A; the pair is identical on these summary beats "
        f"(repeatability **{'PASS' if report.repeatability_pass else 'FAIL'}**).",
        "",
        _summary_table(metrics_a, summary_rows),
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


def _render_variant_summary_only(
    compare,
    *,
    variant: str,
    run_a: str,
    run_b: str,
    summary_rows: tuple[tuple[str, str], ...],
    default_fixture_revision: str,
) -> str:
    metrics_a = _load_metrics(run_a)
    path_a = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_a, REPO_ROOT / "logs" / "device")
    path_b = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_b, REPO_ROOT / "logs" / "device")
    report = compare.compare_run_files(path_a, path_b, run_a=run_a, run_b=run_b)
    compare_text = compare.format_report(report)
    fixture = _fixture_revision(metrics_a, default_fixture_revision)
    return "\n".join(
        [
            f"## `{variant}`",
            "",
            f"Accepted pair: {_run_link(run_a)} / {_run_link(run_b)} · "
            f"fixture `{fixture}` · repeatability **{'PASS' if report.repeatability_pass else 'FAIL'}** · "
            f"[full results]({variant}.md)",
            "",
            _summary_table(metrics_a, summary_rows),
            "```text",
            compare_text.rstrip(),
            "```",
            "",
        ]
    )


def _render_campaign_all_variants(compare) -> str:
    generated = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    blocks: list[str] = [
        "# LAB-16 memory campaign — all VARIANT summaries",
        "",
        f"Generated: {generated} by `render_memory_results.py`. Do not edit by hand; "
        "run `make lab.lab16.results`.",
        "",
        "Campaign index: [README.md](../README.md). Per-run inventory: [runs/README.md](../runs/README.md). "
        "Full per-beat tables live in each linked `results/<variant>.md` file.",
        "",
        "Cells: interval minimum free (or min largest DMA block) in KiB and % of the region heap "
        "total KiB named in each column header.",
        "",
    ]
    for variant, _title, pair, _stress_beat, _stress_label in CAMPAIGN_VARIANTS:
        if len(pair) != 2:
            continue
        blocks.append(
            _render_variant_summary_only(
                compare,
                variant=variant,
                run_a=pair[0],
                run_b=pair[1],
                summary_rows=VARIANT_SUMMARY_ROWS[variant],
                default_fixture_revision=VARIANT_FIXTURE_REVISION[variant],
            )
        )
    return "\n".join(blocks) + "\n"


def _run_link(run_id: str) -> str:
    return f"[{run_id}](../runs/{run_id}/run-manifest.json)"


def _stress_row(variant: str, metrics: dict[str, Any], beat_id: str, label: str) -> str:
    moment = f"`{variant}` — {label}"
    window = (metrics.get("beat_windows") or {}).get(beat_id)
    if window is None:
        return f"| {moment} | — | — | — | — |"
    return (
        f"| {moment} | "
        f"{_min_free_cell(window['internal_min_free'], window['internal_total'])} | "
        f"{_min_free_cell(window['dma_min_free'], window['dma_total'])} | "
        f"{_dma_block_cell(window['dma_min_largest'], window['dma_total'])} | "
        f"{_min_free_cell(window['psram_min_free'], window['psram_total'])} |"
    )


def _index_stress_heap_totals_legend() -> list[str]:
    lines = [
        "**Region heap totals KiB** (run A, baseline or first summary beat — used for % denominators on variant pages):",
        "",
    ]
    for variant, _title, pair, _stress_beat, _stress_label in CAMPAIGN_VARIANTS:
        if len(pair) != 2:
            continue
        metrics_a = _load_metrics(pair[0])
        ref = _reference_window(metrics_a, VARIANT_SUMMARY_ROWS.get(variant))
        if ref is None:
            lines.append(f"- `{variant}`: (not captured)")
            continue
        lines.append(
            f"- `{variant}`: internal {_kib(ref['internal_total'])}, "
            f"DMA {_kib(ref['dma_total'])}, PSRAM {_kib(ref['psram_total'])}"
        )
    lines.append("")
    return lines


def _render_index(compare) -> str:
    variant_rows: list[str] = []
    stress_rows: list[str] = []
    for variant, title, pair, stress_beat, stress_label in CAMPAIGN_VARIANTS:
        run_a, run_b = pair
        results_href = f"[results/{variant}.md](results/{variant}.md)"
        if len(pair) != 2:
            variant_rows.append(
                f"| `{variant}` | {title} | NOT CAPTURED | — | {results_href} |"
            )
            stress_rows.append(
                f"| `{variant}` — {stress_label} | NOT CAPTURED | NOT CAPTURED | "
                "NOT CAPTURED | NOT CAPTURED |"
            )
            continue
        path_a = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_a, REPO_ROOT / "logs" / "device")
        path_b = compare.resolve_metrics_path(REPO_ROOT, EVIDENCE_SLUG, run_b, REPO_ROOT / "logs" / "device")
        report = compare.compare_run_files(path_a, path_b, run_a=run_a, run_b=run_b)
        repeat = "**PASS**" if report.repeatability_pass else "FAIL"
        pair_links = f"{_run_link(run_a)} / {_run_link(run_b)}"
        variant_rows.append(
            f"| `{variant}` | {title} | {pair_links} | {repeat} | {results_href} |"
        )
        metrics_a = _load_metrics(run_a)
        stress_rows.append(_stress_row(variant, metrics_a, stress_beat, stress_label))

    return "\n".join(
        [
            "# LAB-16 evidence — memory campaign results",
            "",
            "Published KiB tables for the [LAB-16 memory campaign]"
            "(../../../client/labs/lab-16-attached-storage/README.md). "
            "Raw artifacts remain under `runs/<run_id>/`; serial captures under "
            "`logs/device/labs/lab-16/`.",
            "",
            "**Repeatability** for accepted pairs is **memory and identity only**; HTTP latency and "
            "phase timing are captured but documented separately — see "
            "[TIMING-AND-NETWORK-NOTES.md](TIMING-AND-NETWORK-NOTES.md).",
            "",
            "Regenerate tables: `make lab.lab16.results` "
            "(runs `client/labs/lab-16-attached-storage/tools/render_memory_results.py`).",
            "",
            "**All VARIANT KiB summaries on one page:** "
            "[results/campaign-all-variants.md](results/campaign-all-variants.md). "
            "Run inventory: [runs/README.md](runs/README.md).",
            "",
            "Compare a pair: `make lab.lab16.compare RUN_A=<run_id> RUN_B=<run_id>`.",
            "",
            "## VARIANT results (accepted pairs)",
            "",
            "| VARIANT | Scenario | Accepted runs (manifests) | Memory repeatability | KiB tables |",
            "|---|---|---|---|---|",
            *variant_rows,
            "",
            "## Peak stress snapshot (run A of each accepted pair)",
            "",
            "Interval **minimum free** during the listed stress beat; KiB and % use that run's "
            "heap total at the beat (per-VARIANT baseline totals listed below).",
            "",
            *_index_stress_heap_totals_legend(),
            "| VARIANT stress moment | Internal min free KiB (%) | DMA min free KiB (%) | "
            "Min largest DMA block KiB (%) | PSRAM min free KiB (%) |",
            "|---|---:|---:|---:|---:|",
            *stress_rows,
            "",
        ]
    )


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
    upload_path.write_text(
        _render_captured_variant(
            compare,
            variant="upload",
            run_a=UPLOAD_ACCEPTED_PAIR[0],
            run_b=UPLOAD_ACCEPTED_PAIR[1],
            summary_rows=UPLOAD_SUMMARY_ROWS,
            default_fixture_revision="lab16-upload/v2",
        ),
        encoding="utf-8",
    )
    written.append(upload_path)

    server_offline_path = RESULTS_DIR / "server-offline-during-record.md"
    if len(SERVER_OFFLINE_ACCEPTED_PAIR) == 2:
        server_offline_path.write_text(
            _render_captured_variant(
                compare,
                variant="server-offline-during-record",
                run_a=SERVER_OFFLINE_ACCEPTED_PAIR[0],
                run_b=SERVER_OFFLINE_ACCEPTED_PAIR[1],
                summary_rows=SERVER_OFFLINE_SUMMARY_ROWS,
                default_fixture_revision="lab16-upload/v2",
            ),
            encoding="utf-8",
        )
    else:
        server_offline_path.write_text(
            _render_stub("server-offline-during-record", "server offline during recording"),
            encoding="utf-8",
        )
    written.append(server_offline_path)

    overlap_path = RESULTS_DIR / "overlap-record-during-upload.md"
    if len(OVERLAP_ACCEPTED_PAIR) == 2:
        overlap_path.write_text(
            _render_captured_variant(
                compare,
                variant="overlap-record-during-upload",
                run_a=OVERLAP_ACCEPTED_PAIR[0],
                run_b=OVERLAP_ACCEPTED_PAIR[1],
                summary_rows=OVERLAP_SUMMARY_ROWS,
                default_fixture_revision="lab16-sd/v4",
            ),
            encoding="utf-8",
        )
    else:
        overlap_path.write_text(
            _render_stub("overlap-record-during-upload", "record message 2 while message 1 uploads"),
            encoding="utf-8",
        )
    written.append(overlap_path)

    live_path = RESULTS_DIR / "live-stream-failover-during-record.md"
    if len(LIVE_FAILOVER_ACCEPTED_PAIR) == 2:
        live_path.write_text(
            _render_captured_variant(
                compare,
                variant="live-stream-failover-during-record",
                run_a=LIVE_FAILOVER_ACCEPTED_PAIR[0],
                run_b=LIVE_FAILOVER_ACCEPTED_PAIR[1],
                summary_rows=LIVE_FAILOVER_SUMMARY_ROWS,
                default_fixture_revision="lab16-diary/v1",
            ),
            encoding="utf-8",
        )
    else:
        live_path.write_text(
            _render_stub("live-stream-failover-during-record", "live outage, SD fallback, and drain"),
            encoding="utf-8",
        )
    written.append(live_path)

    INDEX_PATH.write_text(_render_index(compare), encoding="utf-8")
    written.append(INDEX_PATH)

    campaign_all_path = RESULTS_DIR / "campaign-all-variants.md"
    campaign_all_path.write_text(_render_campaign_all_variants(compare), encoding="utf-8")
    written.append(campaign_all_path)
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
