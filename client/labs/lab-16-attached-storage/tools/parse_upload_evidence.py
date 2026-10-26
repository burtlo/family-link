#!/usr/bin/env python3
"""Parse and validate the LAB-16 default upload variant's device evidence."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

MEM_SCHEMA = "lab16-memory/v3"
EVENT_SCHEMA = "lab16-event/v1"
PARSER = "lab16-upload-evidence-parser/v3"
UPLOAD_REQUIRED_BEATS = (
    "baseline_before_sd",
    "sd_mount_probe",
    "mic_i2s_open_start",
    "mic_i2s_open_complete",
    "sd_mic_recording",
    "wifi_upload_active",
    "upload_complete_workers_quiescent",
)

OVERLAP_REQUIRED_BEATS = (
    "baseline_before_sd",
    "sd_mount_probe",
    "overlap_msg1_mic_i2s_open_start",
    "overlap_msg1_mic_i2s_open_complete",
    "overlap_msg1_recording",
    "overlap_concurrent_peak",
    "overlap_msg2_upload",
    "upload_complete_workers_quiescent",
)

OVERLAP_VARIANT = "overlap-record-during-upload"
LIVE_FAILOVER_VARIANT = "live-stream-failover-during-record"

LIVE_FAILOVER_REQUIRED_BEATS = (
    "baseline_before_sd",
    "sd_mount_probe",
    "live_mic_i2s_open_start",
    "live_mic_i2s_open_complete",
    "live_stream_upload_active",
    "live_failover_outage",
    "live_sd_drain_active",
    "upload_complete_workers_quiescent",
)
_KV = re.compile(r"([a-z][a-z0-9_]*)=([^\s]*)")
_UINT = re.compile(r"0|[1-9][0-9]*")
_SINT = re.compile(r"-?(?:0|[1-9][0-9]*)")
_HEX256 = re.compile(r"[0-9a-f]{64}")
_STACK_TASKS = {
    "main_task": ("main", 12_288, 12_800),
    "sampler_task": ("lab16_mem", 4_096, 4_096),
    "event_loop_task": ("sys_evt", 2_304, 2_816),
    "wifi_task": ("wifi", 6_656, 6_656),
    "tcpip_task": ("tiT", 3_072, 3_584),
}


class ParseError(ValueError):
    pass


def _fields(line: str, prefix: str) -> dict[str, str]:
    body = line[len(prefix):].strip()
    matches = list(_KV.finditer(body))
    if not matches or " ".join(m.group(0) for m in matches) != body:
        raise ParseError(f"malformed {prefix.strip()} record")
    result: dict[str, str] = {}
    for match in matches:
        key, value = match.groups()
        if key in result:
            raise ParseError(f"duplicate {key} in {prefix.strip()} record")
        result[key] = value
    return result


def _int(row: dict[str, str], key: str) -> int:
    value = row.get(key, "")
    if not _UINT.fullmatch(value):
        raise ParseError(f"invalid unsigned integer {key}")
    return int(value)


def _signed_int(row: dict[str, str], key: str) -> int:
    value = row.get(key, "")
    if not _SINT.fullmatch(value):
        raise ParseError(f"invalid signed integer {key}")
    return int(value)


def _http_status_ok(value: object) -> bool:
    return value in (200, "200")


def _coerce(row: dict[str, str], numeric: set[str]) -> dict[str, Any]:
    result: dict[str, Any] = dict(row)
    for key in numeric.intersection(result):
        result[key] = _int(row, key)
    return result


def _beat_windows(samples: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    output: dict[str, dict[str, Any]] = {}
    for row in samples:
        beat = str(row["beat"])
        output[beat] = {
            "sample_count": row["sample_count"],
            "start_t_us": row["t_start_us"],
            "end_t_us": row["t_end_us"],
            "sample_interval_ms": row["sample_interval_ms"],
            "internal_total": row["internal_total"],
            "internal_min_free": row["internal_interval_min_free"],
            "internal_min_free_t_us": row["internal_interval_min_free_t_us"],
            "internal_min_free_offset_us": row["internal_interval_min_free_t_us"] - row["t_start_us"],
            "internal_min_largest": row["internal_interval_min_largest"],
            "internal_min_largest_t_us": row["internal_interval_min_largest_t_us"],
            "internal_min_largest_offset_us": row["internal_interval_min_largest_t_us"] - row["t_start_us"],
            "internal_cumulative_min_free_t_us": row["internal_cumulative_min_free_t_us"],
            "internal_current_free": row["internal_current_free"],
            "internal_current_largest": row["internal_current_largest"],
            "internal_cumulative_min_free_since_heap_init": row["internal_cumulative_min_free_since_heap_init"],
            "internal_cumulative_min_free_delta_from_baseline": row["internal_cumulative_min_free_delta_from_baseline"],
            "dma_total": row["dma_total"],
            "dma_min_free": row["dma_interval_min_free"],
            "dma_min_free_t_us": row["dma_interval_min_free_t_us"],
            "dma_min_free_offset_us": row["dma_interval_min_free_t_us"] - row["t_start_us"],
            "dma_min_largest": row["dma_interval_min_largest"],
            "dma_min_largest_t_us": row["dma_interval_min_largest_t_us"],
            "dma_min_largest_offset_us": row["dma_interval_min_largest_t_us"] - row["t_start_us"],
            "dma_cumulative_min_free_t_us": row["dma_cumulative_min_free_t_us"],
            "dma_current_free": row["dma_current_free"],
            "dma_current_largest": row["dma_current_largest"],
            "dma_cumulative_min_free_since_heap_init": row["dma_cumulative_min_free_since_heap_init"],
            "dma_cumulative_min_free_delta_from_baseline": row["dma_cumulative_min_free_delta_from_baseline"],
            "psram_total": row["psram_total"],
            "psram_min_free": row["psram_interval_min_free"],
            "psram_min_free_t_us": row["psram_interval_min_free_t_us"],
            "psram_min_free_offset_us": row["psram_interval_min_free_t_us"] - row["t_start_us"],
            "psram_min_largest": row["psram_interval_min_largest"],
            "psram_min_largest_t_us": row["psram_interval_min_largest_t_us"],
            "psram_min_largest_offset_us": row["psram_interval_min_largest_t_us"] - row["t_start_us"],
            "psram_cumulative_min_free_t_us": row["psram_cumulative_min_free_t_us"],
            "psram_current_free": row["psram_current_free"],
            "psram_current_largest": row["psram_current_largest"],
            "psram_cumulative_min_free_since_heap_init": row["psram_cumulative_min_free_since_heap_init"],
            "psram_cumulative_min_free_delta_from_baseline": row["psram_cumulative_min_free_delta_from_baseline"],
            "pcm_bytes_max": row["pcm_bytes_max"],
            "sd_driver_writes_max": row["sd_driver_writes_max"],
            "outstanding_buffer_count_max": row["outstanding_buffer_count_max"],
            "outstanding_buffer_bytes_max": row["outstanding_buffer_bytes_max"],
            "request_inflight_max": row["request_inflight_max"],
            "accepted_requests_max": row["accepted_requests_max"],
            "failed_requests_max": row["failed_requests_max"],
            "retries_max": row["retries_max"],
            "main_task_name": row["main_task_name"],
            "main_task_configured_stack_bytes": row["main_task_configured_stack_bytes"],
            "main_task_allocated_stack_bytes": row["main_task_allocated_stack_bytes"],
            "main_task_status": row["main_task_status"],
            "main_task_stack_hwm_sdk_units": row["main_task_stack_hwm_sdk_units"],
            "main_task_stack_hwm_unit": row["main_task_stack_hwm_unit"],
            "sampler_task_name": row["sampler_task_name"],
            "sampler_task_configured_stack_bytes": row["sampler_task_configured_stack_bytes"],
            "sampler_task_allocated_stack_bytes": row["sampler_task_allocated_stack_bytes"],
            "sampler_task_status": row["sampler_task_status"],
            "sampler_task_stack_hwm_sdk_units": row["sampler_task_stack_hwm_sdk_units"],
            "sampler_task_stack_hwm_unit": row["sampler_task_stack_hwm_unit"],
            "event_loop_task_name": row["event_loop_task_name"],
            "event_loop_task_configured_stack_bytes": row["event_loop_task_configured_stack_bytes"],
            "event_loop_task_allocated_stack_bytes": row["event_loop_task_allocated_stack_bytes"],
            "event_loop_task_status": row["event_loop_task_status"],
            "event_loop_task_stack_hwm_sdk_units": row["event_loop_task_stack_hwm_sdk_units"],
            "event_loop_task_stack_hwm_unit": row["event_loop_task_stack_hwm_unit"],
            "wifi_task_name": row["wifi_task_name"],
            "wifi_task_configured_stack_bytes": row["wifi_task_configured_stack_bytes"],
            "wifi_task_allocated_stack_bytes": row["wifi_task_allocated_stack_bytes"],
            "wifi_task_status": row["wifi_task_status"],
            "wifi_task_stack_hwm_sdk_units": row["wifi_task_stack_hwm_sdk_units"],
            "wifi_task_stack_hwm_unit": row["wifi_task_stack_hwm_unit"],
            "tcpip_task_name": row["tcpip_task_name"],
            "tcpip_task_configured_stack_bytes": row["tcpip_task_configured_stack_bytes"],
            "tcpip_task_allocated_stack_bytes": row["tcpip_task_allocated_stack_bytes"],
            "tcpip_task_status": row["tcpip_task_status"],
            "tcpip_task_stack_hwm_sdk_units": row["tcpip_task_stack_hwm_sdk_units"],
            "tcpip_task_stack_hwm_unit": row["tcpip_task_stack_hwm_unit"],
        }
        for task, (_, _, _) in _STACK_TASKS.items():
            output[beat][f"{task}_stack_type_bytes"] = row[f"{task}_stack_type_bytes"]
    return output


MEMORY_CAPTURE_VARIANTS = frozenset({
    "upload",
    "server-offline-during-record",
    OVERLAP_VARIANT,
    LIVE_FAILOVER_VARIANT,
})


def parse_serial(serial: str, *, variant: str = "upload", expected_chunks: int = 210,
                 expected_pcm_bytes: int = 6_720_000) -> dict[str, Any]:
    if variant not in MEMORY_CAPTURE_VARIANTS:
        raise ParseError(f"unsupported memory-capture variant: {variant}")
    is_overlap = variant == OVERLAP_VARIANT
    is_live_failover = variant == LIVE_FAILOVER_VARIANT
    if is_overlap:
        required_beats = OVERLAP_REQUIRED_BEATS
        expected_chunks = 180
        expected_pcm_bytes = 5_760_000
    elif is_live_failover:
        required_beats = LIVE_FAILOVER_REQUIRED_BEATS
        expected_chunks = 180
        expected_pcm_bytes = 5_760_000
    else:
        required_beats = UPLOAD_REQUIRED_BEATS
    if len(serial.encode("utf-8")) > 8 * 1024 * 1024:
        raise ParseError("serial capture exceeds 8 MiB bound")
    lines = serial.splitlines()
    samples: list[dict[str, Any]] = []
    events: list[dict[str, Any]] = []
    ready = False
    stopped_samples: int | None = None
    stopped_windows: int | None = None
    memory_incomplete_reasons: list[str] = []
    functional_incomplete_reasons: list[str] = []
    sd_ok = False
    audio_ok = False
    stream_ok = False
    stream_pcm_bytes = 0
    stream_elapsed_ms: int | None = None
    overlap_ok = False
    live_failover_ok = False
    live_drain_ok = False
    stream1_ok = False
    stream2_ok = False
    for line in lines:
        if line.startswith("LAB16_READY "):
            if is_overlap:
                ready = ("fixture=lab16-sd/v4" in line and "upload_fixture=lab16-upload/v2" in line
                         and "mode=scripted" in line)
            elif is_live_failover:
                ready = ("fixture=lab16-sd/v5" in line and "upload_fixture=lab16-diary/v1" in line
                         and "mode=scripted" in line)
            else:
                ready = ("fixture=lab16-sd/v3" in line and "upload_fixture=lab16-upload/v2" in line
                         and "mode=scripted" in line)
        elif line.startswith("LAB16_SD status=detected "):
            sd_ok = "read_lba0=ok" in line and "probe_writes=0" in line
        elif line.startswith("LAB16_AUDIO status=ok "):
            audio_ok = "silent=0" in line
        elif line.startswith("LAB16_STREAM msg=1 status=complete "):
            stream1_ok = "pcm_bytes=5760000" in line
            if is_overlap:
                stream_ok = stream1_ok
                stream_pcm_bytes = 5_760_000 if stream1_ok else 0
        elif line.startswith("LAB16_STREAM msg=2 status=complete "):
            stream2_ok = "pcm_bytes=5760000" in line
        elif line.startswith("LAB16_STREAM status=complete "):
            stream_ok = True
            match = re.search(r"(?:^| )pcm_bytes=(0|[1-9][0-9]*)(?: |$)", line)
            stream_pcm_bytes = int(match.group(1)) if match else 0
            duration = re.search(r"(?:^| )elapsed_ms=(0|[1-9][0-9]*)(?: |$)", line)
            stream_elapsed_ms = int(duration.group(1)) if duration else None
        elif "LAB16_OVERLAP status=recording_msg2 upload_session=1_active" in line:
            overlap_ok = True
        elif "LAB16_OVERLAP status=complete msg1_session=1 msg2_session=2" in line:
            overlap_ok = True
        elif is_live_failover and "LAB16_FAILOVER seq=" in line:
            live_failover_ok = True
        elif is_live_failover and line.startswith("LAB16_LIVE status=drain complete ") and " failed=0" in line:
            live_drain_ok = True
        elif is_live_failover and line.startswith("LAB16_LIVE status=complete "):
            live_failover_ok = True
        elif line.startswith("LAB16_MEM "):
            fields = _fields(line, "LAB16_MEM ")
            if fields.get("schema") != MEM_SCHEMA:
                raise ParseError("wrong LAB16_MEM schema")
            event = fields.get("event")
            if event == "window":
                numeric = {
                    "seq", "sample_interval_ms", "sample_count", "t_start_us", "t_end_us",
                    "internal_total", "internal_interval_min_free", "internal_interval_min_free_t_us",
                    "internal_interval_min_largest", "internal_interval_min_largest_t_us",
                    "internal_cumulative_min_free_since_heap_init", "internal_cumulative_min_free_t_us",
                    "internal_current_free",
                    "internal_current_largest", "dma_total", "dma_interval_min_free",
                    "dma_interval_min_free_t_us", "dma_interval_min_largest", "dma_interval_min_largest_t_us",
                    "dma_cumulative_min_free_since_heap_init", "dma_cumulative_min_free_t_us",
                    "dma_current_free", "dma_current_largest", "psram_total", "psram_interval_min_free",
                    "psram_interval_min_free_t_us", "psram_interval_min_largest", "psram_interval_min_largest_t_us",
                    "psram_cumulative_min_free_since_heap_init", "psram_cumulative_min_free_t_us",
                    "psram_current_free", "psram_current_largest", "pcm_bytes_max", "sd_driver_writes_max",
                    "outstanding_buffer_count_max", "outstanding_buffer_bytes_max", "request_inflight_max",
                    "accepted_requests_max", "failed_requests_max", "retries_max",
                    "main_task_configured_stack_bytes", "main_task_allocated_stack_bytes",
                    "main_task_stack_hwm_sdk_units", "main_task_stack_type_bytes",
                    "sampler_task_configured_stack_bytes", "sampler_task_allocated_stack_bytes",
                    "sampler_task_stack_hwm_sdk_units", "sampler_task_stack_type_bytes",
                    "event_loop_task_configured_stack_bytes", "event_loop_task_allocated_stack_bytes",
                    "event_loop_task_stack_hwm_sdk_units", "event_loop_task_stack_type_bytes",
                    "wifi_task_configured_stack_bytes", "wifi_task_allocated_stack_bytes",
                    "wifi_task_stack_hwm_sdk_units", "wifi_task_stack_type_bytes",
                    "tcpip_task_configured_stack_bytes", "tcpip_task_allocated_stack_bytes",
                    "tcpip_task_stack_hwm_sdk_units", "tcpip_task_stack_type_bytes",
                }
                row = _coerce(fields, numeric)
                signed = {
                    "internal_cumulative_min_free_delta_from_baseline",
                    "dma_cumulative_min_free_delta_from_baseline",
                    "psram_cumulative_min_free_delta_from_baseline",
                }
                for key in signed:
                    row[key] = _signed_int(fields, key)
                required = numeric | signed | {
                    "schema", "event", "beat",
                    *{f"{task}_name" for task in _STACK_TASKS},
                    *{f"{task}_status" for task in _STACK_TASKS},
                    *{f"{task}_stack_hwm_unit" for task in _STACK_TASKS},
                }
                if set(fields) != required:
                    raise ParseError("LAB16_MEM window field set changed")
                if row["seq"] != len(samples) + 1:
                    raise ParseError("LAB16_MEM window sequence has a gap")
                if row["sample_interval_ms"] != 50 or row["sample_count"] < 1:
                    raise ParseError("LAB16_MEM window omitted the 50 ms sample aggregate")
                if row["t_start_us"] <= 0 or row["t_end_us"] < row["t_start_us"]:
                    raise ParseError("LAB16_MEM window monotonic bounds are invalid")
                if samples and row["t_start_us"] <= samples[-1]["t_end_us"]:
                    raise ParseError("LAB16_MEM window time did not increase")
                for task, (expected_name, configured_bytes, allocated_bytes) in _STACK_TASKS.items():
                    if (row[f"{task}_name"] != expected_name
                            or row[f"{task}_configured_stack_bytes"] != configured_bytes
                            or row[f"{task}_allocated_stack_bytes"] != allocated_bytes
                            or row[f"{task}_stack_hwm_unit"] != "StackType_t_items"
                            or row[f"{task}_stack_type_bytes"] != 1):
                        raise ParseError(f"LAB16_MEM {task} stack identity, size, or units are invalid")
                    status = row[f"{task}_status"]
                    high_water = row[f"{task}_stack_hwm_sdk_units"]
                    if status == "captured":
                        if high_water < 1 or high_water > allocated_bytes:
                            raise ParseError(f"LAB16_MEM {task} stack high-water is outside its stack")
                    elif status == "not_yet_started":
                        if task in {"main_task", "sampler_task"} or high_water != 0:
                            raise ParseError(f"LAB16_MEM {task} was not captured after it should be active")
                    else:
                        raise ParseError(f"LAB16_MEM {task} has an unknown stack capture status")
                if row["beat"] in (
                    "wifi_upload_active",
                    "live_stream_upload_active",
                    "live_failover_outage",
                    "live_sd_drain_active",
                ):
                    for task in ("event_loop_task", "wifi_task", "tcpip_task"):
                        if row[f"{task}_status"] != "captured":
                            memory_incomplete_reasons.append(f"{task}_stack_not_captured_during_upload")
                samples.append(row)
            elif event == "monitor_stopped":
                stopped_samples = _int(fields, "samples")
                stopped_windows = _int(fields, "windows")
            elif event == "capture_incomplete":
                memory_incomplete_reasons.append(fields.get("reason", "unspecified"))
        elif line.startswith("LAB16_EVT "):
            fields = _fields(line, "LAB16_EVT ")
            if fields.get("schema") != EVENT_SCHEMA:
                raise ParseError("wrong LAB16_EVT schema")
            numeric = {"t_us", "session", "seq", "attempt", "pcm_bytes", "wav_bytes",
                       "request_body_bytes", "latency_us", "outstanding_requests", "uploaded",
                       "failed", "expected", "attempts", "accepted", "request_body_pcm_bytes",
                       "response_body_bytes_received", "response_body_read_complete",
                       "requested_bytes", "success", "outstanding_count",
                       "outstanding_bytes"}
            if fields.get("event") == "request_end" and _UINT.fullmatch(fields.get("status", "")):
                numeric = numeric | {"status"}
            row = _coerce(fields, numeric)
            if "response_content_length" in fields:
                row["response_content_length"] = _signed_int(fields, "response_content_length")
            events.append(row)

    if not ready:
        memory_incomplete_reasons.append("missing_upload_ready_marker_or_fixture_identity")
    if not sd_ok:
        functional_incomplete_reasons.append("sd_probe_not_verified")
    if is_overlap:
        if not stream1_ok or not stream2_ok:
            functional_incomplete_reasons.append("overlap_stream_messages_not_verified")
        if not overlap_ok:
            functional_incomplete_reasons.append("overlap_phase_markers_not_verified")
    elif is_live_failover:
        if not stream_ok or stream_pcm_bytes != expected_pcm_bytes:
            functional_incomplete_reasons.append("recorded_pcm_size_not_verified")
        if not audio_ok:
            functional_incomplete_reasons.append("sd_header_readback_or_non_silence_not_verified")
        if not live_failover_ok:
            functional_incomplete_reasons.append("live_failover_markers_not_verified")
        if not live_drain_ok:
            functional_incomplete_reasons.append("live_drain_not_verified")
    elif not stream_ok or stream_pcm_bytes != expected_pcm_bytes:
        functional_incomplete_reasons.append("recorded_pcm_size_not_verified")
    if not is_overlap and not is_live_failover and not audio_ok:
        functional_incomplete_reasons.append("sd_header_readback_or_non_silence_not_verified")
    if not samples or samples[0].get("beat") != "baseline_before_sd":
        memory_incomplete_reasons.append("missing_clean_baseline_before_sd")
    beats = [str(row["beat"]) for row in samples]
    missing_beats = [beat for beat in required_beats if beat not in beats]
    if missing_beats:
        memory_incomplete_reasons.append("missing_memory_beats:" + ",".join(missing_beats))
    if len(beats) != len(set(beats)) or beats != [beat for beat in required_beats if beat in beats]:
        memory_incomplete_reasons.append("memory_beats_out_of_order_or_duplicated")
    if stopped_samples is None or stopped_samples != sum(row["sample_count"] for row in samples):
        memory_incomplete_reasons.append("memory_sampler_did_not_stop_cleanly")
    if stopped_windows is None or stopped_windows != len(samples):
        memory_incomplete_reasons.append("memory_window_count_did_not_stop_cleanly")
    if samples:
        baseline = samples[0]
        for row in samples:
            for region in ("internal", "dma", "psram"):
                watermark = row[f"{region}_cumulative_min_free_since_heap_init"]
                expected_delta = baseline[f"{region}_cumulative_min_free_since_heap_init"] - watermark
                actual_delta = row[f"{region}_cumulative_min_free_delta_from_baseline"]
                if actual_delta != expected_delta:
                    memory_incomplete_reasons.append(f"{region}_watermark_delta_mismatch:{row['beat']}")
                    break

    chunk_source = [row for row in events if row.get("event") == "chunk_source"]
    chunk_terminal = [row for row in events if row.get("event") == "chunk_terminal"]
    source_by_seq: dict[int, dict[str, Any]] = {}
    terminal_by_seq: dict[int, dict[str, Any]] = {}
    for row in chunk_source:
        seq = int(row["seq"])
        session = int(row.get("session", 1))
        key = (session, seq) if is_overlap else seq
        if key in source_by_seq:
            raise ParseError(f"duplicate source hash for sequence {seq}")
        if not _HEX256.fullmatch(str(row.get("pcm_sha256", ""))):
            raise ParseError(f"invalid device PCM hash for sequence {seq}")
        source_by_seq[key] = row
    for row in chunk_terminal:
        seq = int(row["seq"])
        session = int(row.get("session", 1))
        key = (session, seq) if is_overlap else seq
        if key in terminal_by_seq:
            raise ParseError(f"duplicate terminal for sequence {seq}")
        terminal_by_seq[key] = row
    skip_device_chunk_identity = is_live_failover
    if is_overlap:
        for session_id in (1, 2):
            expected_sequences = set(range(expected_chunks))
            source_sess = {int(row["seq"]): row for row in chunk_source if int(row.get("session", -1)) == session_id}
            terminal_sess = {int(row["seq"]): row for row in chunk_terminal
                             if int(row.get("session", -1)) == session_id}
            if set(source_sess) != expected_sequences or set(terminal_sess) != expected_sequences:
                functional_incomplete_reasons.append(f"overlap_session_{session_id}_chunk_sequence_incomplete")
            if any(row.get("accepted") != 1 or row.get("failed") != 0 for row in terminal_sess.values()):
                functional_incomplete_reasons.append(f"overlap_session_{session_id}_chunk_acceptance_failure")
        chunks_complete = not any(item.startswith("overlap_session_") for item in functional_incomplete_reasons)
        upload_complete = (any("LAB16_UPLOAD session=1 status=complete uploaded=180 failed=0" in line for line in lines)
                           and any("LAB16_UPLOAD session=2 status=complete uploaded=180 failed=0" in line for line in lines))
        if not upload_complete:
            functional_incomplete_reasons.append("overlap_upload_session_counts_missing_or_inconsistent")
    elif is_live_failover:
        upload_complete = any(
            "LAB16_UPLOAD session=1 status=complete uploaded=180 failed=0" in line for line in lines
        )
        if not upload_complete:
            functional_incomplete_reasons.append("live_upload_session_counts_missing_or_inconsistent")
        chunks_complete = upload_complete
    else:
        expected_sequences = set(range(expected_chunks))
        if set(source_by_seq) != expected_sequences or set(terminal_by_seq) != expected_sequences:
            functional_incomplete_reasons.append("incomplete_device_chunk_hash_or_terminal_sequence")
        if any(row.get("accepted") != 1 or row.get("failed") != 0 for row in terminal_by_seq.values()):
            functional_incomplete_reasons.append("device_chunk_acceptance_failure")
        upload_terminal = [row for row in events if row.get("event") == "upload_terminal"]
        upload_complete = (len(upload_terminal) == 1 and upload_terminal[0].get("uploaded") == expected_chunks
                           and upload_terminal[0].get("failed") == 0
                           and upload_terminal[0].get("expected") == expected_chunks)
        if not upload_complete:
            functional_incomplete_reasons.append("upload_terminal_counts_missing_or_inconsistent")
        chunks_complete = (set(source_by_seq) == expected_sequences and set(terminal_by_seq) == expected_sequences
                           and all(row.get("accepted") == 1 and row.get("failed") == 0
                                   for row in terminal_by_seq.values()))
    if skip_device_chunk_identity:
        source_by_seq = {}
        terminal_by_seq = {}
    harness_complete = any("LAB16_HARNESS_DONE status=complete" in line for line in lines)
    if not harness_complete:
        functional_incomplete_reasons.append("functional_harness_not_complete")

    explicit_failure = (any("LAB16_HARNESS_DONE status=failed" in line for line in lines)
                        or any("LAB16_AUDIO status=failed" in line or "LAB16_STREAM status=failed" in line
                               for line in lines)
                        or any(row.get("accepted") == 0 or row.get("failed", 0) > 0
                               for row in terminal_by_seq.values())
                        or any(row.get("event") == "upload_terminal" and row.get("failed", 0) > 0
                               for row in events))
    if is_overlap:
        functional_complete = (harness_complete and sd_ok and stream1_ok and stream2_ok and overlap_ok
                               and upload_complete and chunks_complete)
    elif is_live_failover:
        functional_complete = (harness_complete and sd_ok and stream_ok and stream_pcm_bytes == expected_pcm_bytes
                               and audio_ok and live_failover_ok and live_drain_ok
                               and upload_complete and chunks_complete)
    else:
        functional_complete = (harness_complete and sd_ok and stream_ok and stream_pcm_bytes == expected_pcm_bytes
                               and audio_ok and upload_complete and chunks_complete)
    functional_status = "PASS" if functional_complete else "FAIL" if explicit_failure else "UNMEASURED"

    return {
        "document": "family-link.lab-metrics/v2",
        "parser": PARSER,
        "variant": variant,
        "memory_status": "COMPLETE" if not memory_incomplete_reasons else "INCOMPLETE",
        "functional_status": functional_status,
        "incomplete_reasons": memory_incomplete_reasons,
        "functional_incomplete_reasons": functional_incomplete_reasons,
        "required_beats": list(required_beats),
        "observed_beats": beats,
        "memory_samples": samples,
        "beat_windows": _beat_windows(samples),
        "device_events": events,
        "device_chunk_source_count": len(chunk_source) if is_overlap else len(source_by_seq),
        "device_chunk_terminal_count": len(chunk_terminal) if is_overlap else len(terminal_by_seq),
        "device_chunk_pcm_sha256": (
            {f"{row.get('session')}:{row.get('seq')}": row["pcm_sha256"] for row in chunk_source}
            if is_overlap
            else {str(seq): row["pcm_sha256"] for seq, row in sorted(source_by_seq.items())}
        ),
        "expected_pcm_bytes": expected_pcm_bytes,
        "recorded_pcm_bytes": stream_pcm_bytes,
        "expected_chunks": expected_chunks,
        "sampler_stopped_sample_count": stopped_samples,
        "stream_elapsed_ms": stream_elapsed_ms,
        "request_latencies_us": [row["latency_us"] for row in events
                                 if row.get("event") == "request_end" and "latency_us" in row],
    }


def parse_serial_partial(serial: str, *, variant: str = "upload", expected_chunks: int = 210,
                         expected_pcm_bytes: int = 6_720_000) -> dict[str, Any]:
    """Parse the longest valid prefix so a truncated tail cannot discard samples."""
    lines = serial.splitlines()
    try:
        return parse_serial(serial, variant=variant, expected_chunks=expected_chunks,
                            expected_pcm_bytes=expected_pcm_bytes)
    except ParseError as original_error:
        low, high = 0, len(lines)
        while low < high:
            midpoint = (low + high + 1) // 2
            prefix = "\n".join(lines[:midpoint])
            try:
                parse_serial(prefix, variant=variant, expected_chunks=expected_chunks,
                             expected_pcm_bytes=expected_pcm_bytes)
            except ParseError:
                high = midpoint - 1
            else:
                low = midpoint
        metrics = parse_serial("\n".join(lines[:low]), variant=variant, expected_chunks=expected_chunks,
                               expected_pcm_bytes=expected_pcm_bytes)
        metrics["partial_parse_error"] = f"{type(original_error).__name__}: {original_error}"
        metrics["memory_status"] = "INCOMPLETE"
        metrics.setdefault("incomplete_reasons", []).append("serial_parse_stopped_at_invalid_record")
        return metrics


def verify_server_rows(metrics: dict[str, Any], rows: list[dict[str, Any]], *, expected_chunks: int = 210,
                       expected_wav_bytes: int = 32_044) -> list[str]:
    errors: list[str] = []
    expected = set(range(expected_chunks))
    server_by_seq: dict[int, dict[str, Any]] = {}
    for row in rows:
        if str(row.get("session")) != "1":
            continue
        seq = int(row.get("seq", -1))
        if seq in server_by_seq:
            errors.append(f"server duplicate sequence {seq}")
        server_by_seq[seq] = row
    observed = set(server_by_seq)
    missing = expected - observed
    unexpected = observed - expected
    if missing:
        errors.append("server sequence set is incomplete or unexpected")
    if unexpected:
        errors.append("server contains unexpected sequence values")
    device_hashes = metrics.get("device_chunk_pcm_sha256", {})
    for seq in sorted(expected.intersection(server_by_seq)):
        row = server_by_seq[seq]
        if row.get("session") != "1":
            errors.append(f"server sequence {seq} has wrong session")
        if row.get("bytes") != expected_wav_bytes:
            errors.append(f"server sequence {seq} has wrong WAV byte count")
        if row.get("pcm_bytes") != expected_wav_bytes - 44:
            errors.append(f"server sequence {seq} has wrong PCM byte count")
        if not _HEX256.fullmatch(str(row.get("wav_sha256", ""))):
            errors.append(f"server sequence {seq} lacks WAV hash")
        if row.get("pcm_sha256") != device_hashes.get(str(seq)):
            errors.append(f"server sequence {seq} PCM hash differs from device SD read")
        if row.get("status") != 200:
            errors.append(f"server sequence {seq} status is not 200")
        successful_attempts = [event for event in metrics.get("device_events", [])
                               if event.get("event") == "request_end" and event.get("seq") == seq
                               and _http_status_ok(event.get("status"))]
        if len(successful_attempts) != 1:
            errors.append(f"device sequence {seq} lacks exactly one successful response")
        else:
            request = successful_attempts[0]
            if request.get("request_body_bytes") != row.get("request_body_bytes"):
                errors.append(f"sequence {seq} request body size differs between device and server")
            if request.get("response_body_bytes_received") != row.get("response_bytes"):
                errors.append(f"sequence {seq} actual response body bytes differ between device and server")
            if request.get("response_content_length") != row.get("response_bytes"):
                errors.append(f"sequence {seq} response Content-Length differs from server body size")
            if request.get("response_body_read_complete") != 1:
                errors.append(f"sequence {seq} device did not consume the complete response body")
        terminal = next((event for event in metrics.get("device_events", [])
                         if event.get("event") == "chunk_terminal" and event.get("seq") == seq), None)
        if terminal is None or terminal.get("wav_bytes") != expected_wav_bytes \
                or terminal.get("request_body_pcm_bytes") != expected_wav_bytes - 44:
            errors.append(f"device sequence {seq} has wrong PCM or WAV byte count")
    metrics["server_chunk_count"] = len(server_by_seq)
    metrics["server_pcm_sha256_verified"] = not errors
    metrics["server_chunks"] = sorted(server_by_seq.values(), key=lambda row: int(row["seq"]))
    if errors:
        only_missing = set(errors) == {"server sequence set is incomplete or unexpected"}
        if not only_missing or metrics.get("functional_status") == "PASS":
            metrics["functional_status"] = "FAIL"
        else:
            metrics["functional_status"] = "UNMEASURED"
            metrics.setdefault("functional_incomplete_reasons", []).append("server_sequence_set_incomplete")
        metrics.setdefault("functional_verification_errors", []).extend(errors)
    return errors


def parse_file(serial_path: Path, output_path: Path | None = None, *, run_id: str = "lab16-upload-run") -> dict[str, Any]:
    metrics = parse_serial(serial_path.read_text(encoding="utf-8", errors="replace"))
    metrics["run_id"] = run_id
    if output_path is not None:
        output_path.write_text(json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return metrics


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--run-id", default="lab16-upload-run")
    args = parser.parse_args()
    try:
        result = parse_file(args.serial, args.output, run_id=args.run_id)
    except (OSError, ParseError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    print(f"memory_status={result['memory_status']} samples={len(result['memory_samples'])}")
    return 0 if result["memory_status"] == "COMPLETE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
