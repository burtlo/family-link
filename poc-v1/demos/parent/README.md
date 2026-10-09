# Parent-on-the-Mac tools (phone stand-in until Safari exists).

Same protocol as a box: bearer token + HTTP + WebSocket. Default identity is
`box-b` (peer of the flashed `box-a`).

| Script | Job |
|---|---|
| `live_ptt.py` | Live hangout: stream PCM **to** the box, capture PCM **from** the box |
| `send_voicemail.py` | Async: POST a WAV into the box inbox; **h09** plays it |
| `send_photo.py` | Async: POST a JPEG into the box inbox (`kind=image`); combined host serves RGB565 preview |

See [`docs/DEMO-MAP.md`](../../docs/DEMO-MAP.md).
