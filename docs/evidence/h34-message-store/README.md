# h34 message_store evidence

## How to run

From the repo root (with server deps: `make install-server`):

```bash
make demo-message-store
```

Or start the server and client separately:

```bash
export FAMILY_LINK_ROOT="$(pwd)"
python demos/server/h34_message_store/server.py --port 8080
python demos/server/h34_message_store/client.py --base-url http://127.0.0.1:8080
```

## What the client checks

- `POST /v1/messages` with `audio.codec=pcm_s16le`
- Out-of-order chunk PUT with SHA-256 headers (201 / 200 duplicate / 409 conflict)
- `GET …/upload` and `POST …/complete` with `source_audio_sha256`
- Completed tree under `data/v1_product/message_store/messages/YYYY/MM/` with `complete` marker and no leftover `audio/*.chunk`
- Recipient `GET /v1/inbox` and `GET …/audio` with `Accept-Ranges` and `206` partial content
- Admin cull preview → cull → restore (manifest/media hash unchanged)
- Subprocess restart test: open upload survives restart; completed WAV survives second restart

## PASS line

Look for:

```text
-- PASS h34_message_store message_id=… chunk_bytes=… wav_bytes=…
-- PASS restart_survival
```
