"""LAB-16 capture verification (ACC-NEG)."""
from __future__ import annotations

import re

from device_guardrails.serial_common import serial_forbidden_errors, stream_duration_errors

# Default upload variant (CONFIG_LAB16_CAPTURE_EVIDENCE): v3 SD + v2 upload fixture.
UPLOAD_READY = re.compile(
    r"LAB16_READY build=\S+ mode=scripted fixture=lab16-sd/v3 "
    r"upload_fixture=lab16-upload/v2 harness=1"
)
SD_OK = re.compile(
    r"LAB16_SD status=detected sectors=\d+ sector_bytes=512 capacity_bytes=\d+ "
    r"bus_width=\d+ real_freq_khz=\d+ read_lba0=ok probe_writes=0"
)
STREAM_OK = re.compile(
    r"LAB16_STREAM status=complete elapsed_ms=\d+ pcm_bytes=6720000 "
    r"sectors_written=\d+ driver_writes=\d+ start_lba=\d+ sample_rate=16000"
)
AUDIO_OK = re.compile(r"LAB16_AUDIO status=ok\b.*\bsilent=0\b")
UPLOAD_OK = re.compile(
    r"LAB16_UPLOAD (?:session=\d+ )?status=complete uploaded=210 failed=0"
)
HARNESS_OK = re.compile(r"LAB16_HARNESS_DONE status=complete")


def verify_lab16_upload_capture(text: str) -> list[str]:
    """Serial smoke checks for the default upload scripted harness."""
    errors = serial_forbidden_errors(text)
    errors.extend(stream_duration_errors(text))
    if not UPLOAD_READY.search(text):
        errors.append("missing LAB16_READY upload harness (v3 SD + lab16-upload/v2)")
    if not SD_OK.search(text):
        errors.append("missing successful LAB16_SD detected line")
    if not STREAM_OK.search(text):
        errors.append("missing LAB16_STREAM status=complete with 6720000 PCM bytes")
    if not AUDIO_OK.search(text):
        errors.append("missing LAB16_AUDIO status=ok with silent=0")
    if not UPLOAD_OK.search(text):
        errors.append("missing LAB16_UPLOAD session upload complete uploaded=210 failed=0")
    if not HARNESS_OK.search(text):
        errors.append("missing LAB16_HARNESS_DONE status=complete")
    return errors


def verify_lab16_capture(text: str) -> list[str]:
    """Backward-compatible alias for guardrail tests and upload verification."""
    return verify_lab16_upload_capture(text)
