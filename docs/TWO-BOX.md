# Two boxes through the server

The protocol already treats every endpoint as `device_id` + bearer token. A second ESP32-S3-BOX-3 is the same firmware as the first. There is **no** box-to-box Wi-Fi stream.

## Tokens

`devices.example.yaml`:

| Kit | `DEMO_DEVICE_ID` | `DEMO_DEVICE_TOKEN` | Peer |
|---|---|---|---|
| Child A | `box-a` | `change-me-a` | box-b |
| Child B | `box-b` | `change-me-b` | box-a |

Flash **h11** (or **x01**) on each kit with that kit’s `firmware/secrets.h`. Same `DEMO_SERVER_HOST` (this Mac’s LAN IP).

## Run

```
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
```

If combined is not up yet, `python -m demos.server.06_audio_relay.server --host 0.0.0.0 --port 8080` is hangout-only.

Start both boxes. One invites (h11 invites on hello); the other should accept a `ring`. Hold mute to talk; speaker plays only while mute is up.

v1 **product** hangout is still you (phone/Mac) + one child. Two boxes here prove the relay, not kid-to-kid messaging as a feature. Inboxes stay per child.
