# LAB-16 — SD-buffered capture + h22 diary upload

Make name: `lab16`. **Scripted only.** The host starts an h22-compatible diary server (`/v1/diary`, port **8776**) when you run the lab.

## Flow

1. Post-flash settle (`LAB16_BOOT`).
2. SD probe (LBA0 read).
3. **210 s** mic → raw sectors on the card (`lab16-sd/v3`).
4. Wi-Fi join, then **210 × 1 s WAV chunks** read back from SD and POSTed to the diary server.
5. `LAB16_HARNESS_DONE status=complete` when stream + upload succeed.

## Variants

| Variant | Host behavior |
|---------|----------------|
| `upload` (default) | Diary server up before flash |
| `server-offline-during-record` | Server starts only after `LAB16_STREAM status=complete` (recording never depends on the network) |
| `overlap-record-during-upload` | Two **180 s** messages: upload session 1 while recording message 2 to SD (server up throughout) |
| `live-stream-failover-during-record` | One **180 s** take: stream 1 s chunks live; host simulates server outage; SD buffer + drain |

## Run

```text
make lab.lab16 KIT=<kit> PORT=<port>
make lab.lab16 VARIANT=server-offline-during-record KIT=<kit> PORT=<port>
make lab.lab16 VARIANT=overlap-record-during-upload KIT=<kit> PORT=<port>
make lab.lab16 VARIANT=live-stream-failover-during-record KIT=<kit> PORT=<port>
```

Default capture window: **720 s** (210 s record + upload + margin). Requires deployment Wi-Fi + endpoint token for the kit (`lab16_build` overlay).

Verify:

```text
python client/labs/lab-16-attached-storage/tools/verify_capture.py <serial.txt>
```

Server code: [`server/labs/lab-16-attached-storage/`](../../../server/labs/lab-16-attached-storage/server.py). Chunks land under `data/labs/lab-16/<run_id>/`.

## Desk results (lynn-kit, COM5, 2026-10-23)

Recorded host runs with `workflow_status=PASS` / `experiment_verdict=PASS`. Evidence under `docs/labs/evidence/lab-16/runs/<run_id>/`; serial under `logs/device/labs/lab-16/<run_id>/serial.txt`.

| Variant | Run ID | Notes |
|---------|--------|--------|
| `upload` | `20261023-upload-run-b193b616` | 210 s SD → 210 chunks session 1 (`lab16-sd/v3`) |
| `server-offline-during-record` | `20261023-server-offline-during-record-run-d9566b03` | Server late-start; full SD capture before upload |
| `overlap-record-during-upload` | `20261023-overlap-record-during-upload-run-8d644d2f` | 2×180 s; record msg2 during upload msg1 (`lab16-sd/v4`) |
| `live-stream-failover-during-record` | `20261023-live-stream-failover-during-record-run-77755e4a` | 180 s live stream; host `accepting=false` mid-take; SD failover + drain; 180/180 on server (`lab16-sd/v5`) |

Verify per variant:

```text
python client/labs/lab-16-attached-storage/tools/verify_capture.py <serial.txt>
python client/labs/lab-16-attached-storage/tools/verify_overlap_capture.py <serial.txt>
python client/labs/lab-16-attached-storage/tools/verify_live_stream_capture.py <serial.txt>
```
