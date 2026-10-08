# Three-box USB handoff (2026-10-08)

Operator plan for bringing up **three ESP32-S3-BOX-3** units **one at a time** on USB. Use this in a fresh agent session after plugging **only one** box (main-unit USB-C, not dock power-only).

**Not in this session:** **Arlo** (`box-b`) — separate hardware at the office; bind later with `WHO=arlo` and `PORT=`. Do not confuse Arlo with **Lynn’s** storage-qual unit below.

**Canonical USB roster:** [`kits.local.yaml`](../../kits.local.yaml) (gitignored). **Storage overview:** [`ATTACHED-STORAGE.md`](../ATTACHED-STORAGE.md).

---

## The three boxes present

| Person | Flash `WHO=` | Kit id(s) in `kits.local.yaml` | USB serial (stable) | microSD / SENSOR | Attached-storage status |
|--------|----------------|--------------------------------|---------------------|------------------|-------------------------|
| **Mazi** | `mazi` | `box-a` | `E8:F6:0A:A8:D2:98` | 32 GB card — **H38 bounded pass** on this card | **Done** for Stage B (`io_complete`). Public evidence: [`h38-32gb-20261008`](../evidence/attached-storage-qualification/h38-32gb-20261008/). Firmware on desk: **h31 Wi‑Fi demo** (not x02). Backup: `~/family-link-storage-experiments/family-link-attached-discovery-backup-20261007` |
| **Lynn** | `lynn` or product `endpoint-lynn` | `endpoint-lynn`, `lynn` | `E8:F6:0A:A8:B0:48` | 32 GB card — **H35 + H37 prep** on this hardware; **H38 not run** | Prep manifest: `~/family-link-storage-experiments/box-b-qual-prep.json`. Private dirs still named `h35-boxb-*` / `h37-boxb-*` / backup `…-backup-box-b-20261008` (immutable epochs). **Next qual gate:** H38 bounded run on this card |
| **Audrey** | `audrey` (when added to hangout) | `endpoint-audrey`, `audrey` | `E8:F6:0A:A8:AD:7C` | Card present; **no qual epochs** | **Greenfield:** full backup → H35 → H37 → (later H38). Do **not** reuse Mazi or Lynn private captures |

**Naming correction:** Early chat called Lynn’s unit “Box B” for parallel qual; **Arlo** (`box-b`) is a **different** box at the office. Lynn’s qual lineage is tied to USB serial **`B0:48`**, not Audrey’s **`AD:7C`**.

---

## One-box-at-a-time procedure (agent)

For each session, **unplug the other boxes** so exactly **one** `/dev/cu.usbmodem*` appears (avoids wrong-port flashes and qual mistakes).

1. **Enumerate**
   ```bash
   ls /dev/cu.usbmodem*
   cd /path/to/family-link && .venv/bin/python scripts/device.py device-detect
   ```
   Confirm **one** device and that **USB serial** matches the table row for the person you intend.

2. **Connect (ROM sync + chip)**
   ```bash
   PORT=/dev/cu.usbmodemXXXX make connect
   ```
   Expect ESP32-S3, MAC matching the serial (lowercase, colon-separated).

3. **Bind kit** (updates `kits.local.yaml` `last_port`; serial should already match if roster is correct)
   ```bash
   make flash DEMO=h26 WHO=mazi PORT=/dev/cu.usbmodemXXXX
   ```
   Use `WHO=mazi` | `WHO=lynn` | `WHO=audrey` as appropriate. A minimal demo flash is enough to call `remember_usb`; use `DEMO=x02` when ready for product shell.

4. **Optional deeper read**
   ```bash
   PORT=/dev/cu.usbmodemXXXX make report
   ```

5. **Record** in the session notes: observed port, serial, project name from boot (e.g. h31), and whether SENSOR + microSD are mounted.

Repeat for the next physical box after unplugging the first.

---

## Per-box goals for the next agent

### Mazi (`D2:98`)

- **Verify** identity and h31 (or intended) firmware; **no H38 rerun** unless starting a **new** epoch on purpose.
- **Do not** point global `current-h35-capture.json` at another card without operator intent (Mazi’s H35 pointer remains the passed 32 GB lineage for **this** card).

### Lynn (`B0:48`)

- **Verify** SENSOR + same 32 GB card; confirm backup fingerprint still matches (`family-link-attached-discovery-backup-box-b-20261008`).
- **Next storage work:** H38 `preflight` → `prepare` → `build` → `validate` → `run` with explicit:
  - `--backup-dir ~/family-link-storage-experiments/family-link-attached-discovery-backup-box-b-20261008`
  - `--h35-capture-dir ~/family-link-storage-experiments/h35-boxb-20261008`
  - `--h37-run-dir ~/family-link-storage-experiments/h37-boxb-20261008`
  - `--port` for this serial only
- See [`ATTACHED-STORAGE.md`](../ATTACHED-STORAGE.md) operator workflow; git tree must be clean for H38 `prepare`.

### Audrey (`AD:7C`)

- **First-time:** `h32_storage_qual.py backup` → new private backup dir under `~/family-link-storage-experiments/`.
- **Then** H35 → H37 on **her** card (new run dirs; do not use `h35-boxb-*` or Mazi’s `h35-32gb-20261007`).
- H38 only after H37 `read_complete` and verified restore on **this** BOX.

---

## USB / hub notes

- **Serial is identity;** `cu.usbmodem*` **changes** when replugged or moved between hub and direct Mac port.
- With **multiple** boxes plugged in, **always** set `PORT=` or rely on `usb_serial` in `kits.local.yaml` after each kit is bound once.
- If Lynn (`B0:48`) does not appear when expected: main-unit **data** USB-C, try **Reset**, single-box session.

---

## Arlo (fourth unit, office — out of scope for “three present”)

- Kit id **`box-b`**, `WHO=arlo` for kid demos / tokens in [`TWO-BOX.md`](../TWO-BOX.md).
- **No `usb_serial` in roster yet.** Tomorrow at office: one-box `device-detect` → `PORT=… make connect` → `make flash WHO=arlo PORT=…`.
- Arlo is **not** the Lynn storage-prep hardware (`B0:48`) unless operator later confirms they are the same physical unit (they are documented as **different** as of 2026-10-08).

---

## Quick reference — private paths (Lynn storage track)

| Artifact | Path |
|----------|------|
| BOX backup | `~/family-link-storage-experiments/family-link-attached-discovery-backup-box-b-20261008` |
| H35 epoch | `~/family-link-storage-experiments/h35-boxb-20261008` |
| H37 epoch | `~/family-link-storage-experiments/h37-boxb-20261008` |
| Prep summary | `~/family-link-storage-experiments/box-b-qual-prep.json` |

Mazi passed-track private dirs remain under `h35-32gb-20261007`, `h37-32gb-20261007b`, backup `family-link-attached-discovery-backup-20261007` (see [`ATTACHED-STORAGE.md`](../ATTACHED-STORAGE.md)).
