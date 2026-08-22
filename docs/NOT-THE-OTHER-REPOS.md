# Why this is a new tree

| Repo | What it optimized for | What we actually need |
|---|---|---|
| `eink-family-messenger` | Five canned phrases, Waveshare 2.9″ e-ink, breadboard, local Node poll every 5 s, matching boxes on both ends | Free-form **audio + photos + live PTT**, parent on a **phone**, per-child desk, PIN inbox |
| `eink-device-landscape` | Pocket e-ink communicator with cellular + Gmail; T-Deck vs HiBreak vs Minimal Phone | **No cellular**, no Gmail, not a phone, not e-ink. Wi-Fi desk HMI with **mics and a speaker** |

Reuse at most: “a server you own” and “pair a device identity.” Do not reuse the Arduino e-ink firmware, the five-button UX, or the phone-shopping matrix.

ESP32-S3-BOX-3 was not in either repo. It showed up when the product became a **desk answering machine + hangout**, not a badge and not a Gmail handheld.
