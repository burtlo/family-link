# v1 product BOX-3 web twin

Browser stand-in for the **x02** carousel UI. Same v1 server as the kit.

```bash
python -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
open http://localhost:8080/box/
```

## What matches firmware

- 20px top/bottom ribbons (`3/7` count, toast, `shoulder = back`)
- Header row: `From` + portrait + sender name; message cards use the same 66px portrait as sign-in roster cards, plus unread dot and duration `M:SS`
- Center card: face pane + play disk + scrub track
- Shoulder (Boot) → settings carousel (logout, volume, icons, colors, card); centered card opens modal with (X)
- Short tap red circle → recipient picker; tap again → send receipt (Finishing / Sending... / Sent)
- `PUT /v1/profile` for accent + avatar slot

## Keys

| Key | Action |
|---|---|
| B / N / → | Shoulder |
| C / Space | Red circle (short tap) |
| ← / ↓ | Older / newer message |
| P | Play / pause |

## Avatars

Optional PNGs in `avatars/` from `python3 scripts/gen_avatars.py` (needs Pillow). Without PNGs, geometry fallbacks use accent colors.
