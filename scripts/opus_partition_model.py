#!/usr/bin/env python3
"""Reproducible partition capacity and headroom math for X02 Opus feasibility.

Usage: python scripts/opus_partition_model.py

Emits exact byte counts from hex boundaries and planning-model usable capacity.
"""

from __future__ import annotations

FLASH_END = 0x1000000
MSG_PLANNING_BYTES = 389_120  # mid of 360–400 KiB Opus message planning size
FS_WL_PERCENT = 92
FS_SAFETY_PERCENT = 92
APP_SIZE_ALIGNMENT = 0x1000

PROBE_BYTES = 1_665_808
X02_BYTES = 1_484_272
CURRENT_APP = 1_536_000

RESERVE_LOW_KIB = 80
RESERVE_MID_KIB = 125
RESERVE_HIGH_KIB = 178


def hex_bytes(label: str, start: int, end: int) -> int:
    n = end - start
    print(f"{label}: 0x{start:X}..0x{end:X} = 0x{n:X} = {n:,} B")
    return n


def usable_modeled(raw: int) -> int:
    """Apply the 92% × 92% planning factors and floor fractional bytes."""
    return raw * FS_WL_PERCENT * FS_SAFETY_PERCENT // 10_000


def required_margin(slot: int) -> int:
    return max(262_144, (15 * slot + 99) // 100)


def min_slot_15pct(projected: int) -> int:
    """Self-consistent minimum slot when 15% branch dominates (slot >= projected/0.85)."""
    return (100 * projected + 84) // 85


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def main() -> None:
    print("=== Current SINGLE_APP_LARGE tail ===")
    factory_end = 0x10000 + 0x177000
    hex_bytes("Unpartitioned tail", factory_end, FLASH_END)

    print("\n=== Strategy (a) single factory + outbox ===")
    app_a = 0x220000
    outbox_a_raw = hex_bytes("Raw outbox (a)", 0x230000, FLASH_END)
    usable_a = usable_modeled(outbox_a_raw)
    print(f"Modeled usable (a): {usable_a:,} B ({usable_a / (1024 * 1024):.3f} MiB)")
    print(f"3-min messages (a): {usable_a / MSG_PLANNING_BYTES:.1f}")

    slot_a = app_a
    margin_a = required_margin(slot_a)
    print(f"Required margin for 0x{slot_a:X} slot: {margin_a:,} B")

    for name, reserve_kib in (
        ("low", RESERVE_LOW_KIB),
        ("mid", RESERVE_MID_KIB),
        ("high", RESERVE_HIGH_KIB),
    ):
        projected = PROBE_BYTES + reserve_kib * 1024
        free = slot_a - projected
        beyond = free - margin_a
        ok = free >= margin_a
        print(
            f"Reserve {name} ({reserve_kib} KiB): projected={projected:,} "
            f"free={free:,} beyond_margin={beyond:,} product_ready={ok}"
        )

    print("\n=== Self-consistent minimum app slots ===")
    print("Policy: free >= max(256 KiB, ceil(15% of slot))")
    print(f"Rounded slot alignment: 0x{APP_SIZE_ALIGNMENT:X} ({APP_SIZE_ALIGNMENT:,} B)")
    for name, reserve_kib in (
        ("low", RESERVE_LOW_KIB),
        ("mid", RESERVE_MID_KIB),
        ("high", RESERVE_HIGH_KIB),
    ):
        projected = PROBE_BYTES + reserve_kib * 1024
        minimum = min_slot_15pct(projected)
        aligned = align_up(minimum, APP_SIZE_ALIGNMENT)
        print(
            f"Reserve {name} ({reserve_kib} KiB): projected={projected:,} "
            f"minimum={minimum:,} B ({minimum / (1024 * 1024):.6f} MiB) "
            f"aligned=0x{aligned:X} = {aligned:,} B ({aligned / (1024 * 1024):.6f} MiB)"
        )

    print("\n=== Strategy (b) corrected dual OTA + outbox ===")
    hex_bytes("Raw outbox (b)", 0x460000, FLASH_END)
    outbox_b_raw = 0xBA0000
    usable_b = usable_modeled(outbox_b_raw)
    print(f"Modeled usable (b): {usable_b:,} B")
    print(f"3-min messages (b): {usable_b / MSG_PLANNING_BYTES:.1f}")
    print(
        f"Delta vs (a): raw {outbox_a_raw - outbox_b_raw:,} B, "
        f"usable {usable_a - usable_b:,} B, "
        f"messages ~{(usable_a - usable_b) / MSG_PLANNING_BYTES:.1f}"
    )

    print("\n=== Current partition policy (1.5 MiB slot) ===")
    margin_cur = required_margin(CURRENT_APP)
    print(f"Required margin for current slot: {margin_cur:,} B")
    print(f"X02 free: {CURRENT_APP - X02_BYTES:,} (marginal: {CURRENT_APP - X02_BYTES < margin_cur})")
    print(f"Probe overflow: {PROBE_BYTES - CURRENT_APP:,} B")


if __name__ == "__main__":
    main()
