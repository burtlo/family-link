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

## Run

```text
make lab.lab16 KIT=<kit> PORT=<port>
make lab.lab16 VARIANT=server-offline-during-record KIT=<kit> PORT=<port>
make lab.lab16 VARIANT=overlap-record-during-upload KIT=<kit> PORT=<port>
```

Default capture window: **720 s** (210 s record + upload + margin). Requires deployment Wi-Fi + endpoint token for the kit (`lab16_build` overlay).

Verify:

```text
python client/labs/lab-16-attached-storage/tools/verify_capture.py <serial.txt>
```

Server code: [`server/labs/lab-16-attached-storage/`](../../../server/labs/lab-16-attached-storage/server.py). Chunks land under `data/labs/lab-16/<run_id>/`.
