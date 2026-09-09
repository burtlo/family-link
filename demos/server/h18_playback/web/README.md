# BOX-3 web twin (h18 playback)

Browser stand-in of the 320×240 child LCD plus bezel controls. Same catalog
as firmware **h18** (`GET /demo/h18/message?i=N` and the WAV at `url`).

Leave the host running:

```
python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080
open http://localhost:8080/box/
```

| Input | Maps to |
|---|---|
| `B` / `N` / `→`, or Boot on the left | Next clip (wraps) |
| `M`, or mute on the top | Latch: away / open (LED). Not hold-to-talk |
| Hold `C` / Space, or the red circle | Record / PTT (unused on this screen) |
| Click / drag on the LCD | Play, timeline scrub, ROOMVOL |

Reset reloads the page (reboot). Scale 1× / 2× / 3× is viewing only; the
glass stays 320×240 CSS pixels.
