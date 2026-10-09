# h34 — canonical message_store (PCM path)

Host-only Phase 3 proof: [`MESSAGE-PROTOCOL.md`](../../../docs/MESSAGE-PROTOCOL.md) chunk lifecycle with [`SERVER-MESSAGE-STORAGE.md`](../../../docs/SERVER-MESSAGE-STORAGE.md) layout under `data/v1_product/message_store/`.

## Run

```bash
python scripts/run_server_demo.py h34_message_store
make demo-message-store
```

Bearer auth uses **`devices.local.yaml`** / **`devices.example.yaml`** (`change-me-a`, `change-me-b`).

## Layout

```text
message_store/
  incoming/<message-id>/manifest.json  audio/  work/
  messages/YYYY/MM/<message-id>/manifest.json  media.wav  complete
  trash/YYYY/MM/<message-id>/…
  state/inboxes.jsonl
```

PCM chunks finalize to **`media.wav`** (streaming write); chunk files are removed after commit. Server restart rebuilds the in-memory index from manifests and does **not** wipe disk.
