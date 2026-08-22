# Requirements

Source: the product conversation (split household, Marco Polo access failure, Switch/Minecraft hangouts, Wi-Fi already known). Not invented canned phrases.

## Problem

Two children. Communication with you currently rides on **the other parent’s phone** (Marco Polo, and voice during Minecraft on Nintendo Switches). Kids take turns. You do not have a private channel with each child.

## Goals

Keep a small, ongoing connection:

- Each child has **their own endpoint** that lives on a desk (next to a Switch dock is fine).
- You stay on **your phone**.
- No cellular. **2.4 GHz Wi-Fi** only. SSID and password are already known; they can be baked in ahead of a visit or while hanging out there.
- The other parent should not have to unlock a phone or relay messages. They only need the box left plugged in on that network.

## Non-goals (v1)

- Not a smartphone, tablet, or app store.
- Not video (Marco Polo’s medium). Photos are enough for “look at this.”
- Not e-ink, not a pocket daily driver, not Gmail.
- Not Nintendo Switch Online. The box does not run Nintendo’s voice app. Voice to you sits **beside** the game.
- Not always-listening / wake-word. The mic is live only while a button is held. That matters in someone else’s house.
- Not one shared box with two PINs. That recreates the handoff.

## Functional requirements

### Identity

- One physical device = one child.
- You address Child A or Child B from your phone.
- A numeric **passcode** unlocks stored inbound content on that box (sibling / walk-by privacy). Recording out does not need a PIN if the box is theirs.
- Idle timeout relocks. Locked idle may show a **count** (“2 new”), never the body, never audio.

### Async updates (store-and-forward)

| Direction | Media | Notes |
|---|---|---|
| Child → you | Audio clip | Hold to record, release to send. Notification on your phone. |
| You → child | Text, audio clip, **small photo** | Box lights “new.” Child enters PIN, then reads / plays / sees. |
| Child → you | Small photo | Desired. Hardware has **no onboard camera**; see hardware doc. May slip to a later phase if a USB camera is not on the desk. |

Photos are snapshots, not a camera roll. Downscale on the server to something a 320×240 screen can show.

### Live hangout (push-to-talk)

- You start a session from your phone (“Dad is here”).
- The child’s box shows that the hangout is live.
- **Hold to talk**, speaker plays you. Half-duplex on purpose (box sits next to a TV/Switch; full-duplex will echo).
- Mic is dead unless the button is down.
- You end the session, or it times out. Box returns to answering-machine mode.

v1 hangout is **you + one box**. Two kids in Minecraft at once can wait for the group phase.

### Group hangout (later)

- You + more than one child box on the same live session.
- Mixing happens on **your server**, not by turning the boxes into a conference phone.
- Same PTT discipline. Stored inbox stays per-child.

## Constraints

| Constraint | Decision |
|---|---|
| Radio | Wi-Fi 2.4 GHz only. No 5 GHz, no cellular. |
| Power | USB wall power. Always on the desk. |
| Parent client | Phone (PWA or simple app). |
| Backend | A server you own. Device-scoped auth. Audio and photos as files, not a third-party video app. |
| Privacy | Button-gated mic. PIN for playback. No wake word. |

## Success

A child can leave you a voice note, see a photo or text you sent, and talk to you while playing Minecraft **without picking up the other parent’s phone**.
