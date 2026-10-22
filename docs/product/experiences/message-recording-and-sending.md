# Message recording and sending experience

Status: interview draft — reconstruction, ordinary-flow, exception, and
contract checkpoints confirmed 2026-10-21; sender-copy and refusal-vs-wait
meanings, recipient-picker presentation, and sending-feedback treatment
accepted 2026-10-21
Experience ID: EXP-MESSAGE-RECORD-SEND
Revision: 1.6
Owner: Product owner
Approval: Contract checkpoint confirmed 2026-10-21 for the recorded
locked behavior. Sender-copy, refusal-vs-wait, recipient-picker, and
sending-feedback decisions accepted 2026-10-21. No product-wide approval
claimed; no implementation authorization implied

## 1. Identity, scope, and authority

This hardware-mediated and transactional experience is **only for a signed-in
user**. It starts when that user presses the physical red recording button on
the signed-in message inbox and ends when they return to that inbox after
sending or cancelling.

It covers recipient selection, concurrent audio recording and optional
sketching, the continuous preparing/sending/sent sequence, cancellation, and
return to the inbox. It does not specify inbox browsing, playback, settings,
login, or recipient-side delivery and playback.

`MUST` expresses a requirement only after its decision is accepted. V1
record/send evidence and LAB-10 lab artifacts are historical or experimental
and do not establish v2 product policy unless the owner adopts a specific
statement.

### Included

- Entry from the signed-in message inbox via the physical red recording
  button.
- Recipient selection using the established split-panel carousel pattern.
- Recording audio and creating an optional sketch together.
- One continuous preparing → sending → sent confirmation sequence.
- Automatic return to the message inbox after successful server acceptance.
- Cancellation that must not send a partial message.

### Excluded

- Inbox browsing, selection, and playback (`EXP-MESSAGE-CAROUSEL`).
- Sketch logical geometry, color rail, and playback framing
  (`EXP-SKETCH-RECORD-PLAYBACK`), except as inherited constraints.
- Login, mute hardware meaning, and signed-out connectivity
  (`EXP-LOGIN`).
- Settings.
- Recipient notification, playback, or delivery acknowledgment after the
  server accepts the message.
- Wire schemas, codecs, and upload chunking (communication-contract work).

### Experience classes

Hardware-mediated, transactional, and continuous (capture and upload can
outlive a single screen paint).

### Sources

| ID | Source and authority | Relevant material |
|---|---|---|
| SRC-OWNER-001 | Product owner, 2026-10-21 interview brief | Inbox entry; recipient carousel; record/sketch; preparing/sending/sent; cancellation; connectivity reuse; lab.lab10 transition as the product motion |
| SRC-OWNER-002 | Product owner, 2026-10-21 reconstruction checkpoint | Confirmed this is the inbox's send journey; inherit locked mute, connectivity copy, and sketch canvas; use the `lab.lab10` physical ring-wipe enter and reverse collapse |
| SRC-OWNER-003 | Product owner, 2026-10-21 ordinary-flow interview | Tap-to-toggle recipients; no Everyone card; no long-press; none pre-selected; focus vs selection; header copy; other hangout members only; empty `No people to message` |
| SRC-OWNER-004 | Product owner, 2026-10-21 ordinary-flow interview | User-visible recording and sketching begin when the ring-wipe ends; leading audio may be captured earlier and clipped; reverse collapse returns to split pane; sending header is `PANEL-HEADER-TITLE-SUBTITLE` with `sending to ...` and the same recipient names; confirmation dwells 2 seconds then returns to the previous inbox selection paused |
| SRC-OWNER-005 | Product owner, 2026-10-21 ordinary-flow interview | Lower body `preparing` / `sending` / `sent` status copy; no record timer or indicator; 3-minute auto-finish; no silence auto-stop |
| SRC-OWNER-006 | Product owner, 2026-10-21 ordinary-flow interview | Boot during wipe cancels and discards; red ignored during wipe; red and Boot ignored during preparing, sending, and `sent` |
| SRC-OWNER-007 | Product owner, 2026-10-21 exception interview | Offline recipient selection and recording allowed assuming local store is possible; recording continues through connectivity loss; sending wait uses `connecting` / `no Wi-Fi` in the body and does not show `sent` |
| SRC-OWNER-008 | Product owner, 2026-10-21 exception interview | Keep finished work until this same send is accepted; auto-send when the server can accept; Boot during connectivity wait returns to inbox without discarding; skip `sent` if already left; red from inbox while pending returns to the wait screen |
| SRC-OWNER-009 | Product owner, 2026-10-21 exception interview | Mute during recording discards; mute after finish keeps the send and resumes after that user signs in; definite server refusal is atomic `couldn't send`, then discard |
| SRC-OWNER-010 | Product owner, 2026-10-21 exception interview | Power-loss discards unfinished capture and keeps a finished send if storage survives; selected people who leave the hangout are dropped before freeze; a full sketch ignores further drawing and keeps audio |
| SRC-OWNER-011 | Product owner, 2026-10-21 exception checkpoint | Capture cannot start → `couldn't record` and keep selection; mid-recording mic failure finishes and sends; named engineering items delegated |
| SRC-OWNER-012 | Product owner, 2026-10-21 contract checkpoint | Confirmed the locked ordinary journey, cancel rules, truthful `sent`, keep/resume policy, and delegated engineering set |
| SRC-OWNER-013 | Product owner, 2026-10-21 communication-contract interview | Recipients-only publication (no sender inbox copy); timeouts/5xx/expired session keep waiting; unsupported/too-large media and identity-conflict are `couldn't send` |
| SRC-OWNER-014 | Product owner, 2026-10-21 recipient-picker follow-up | Selected cards use the 2 px accent outline alone; when selected names exceed the 25-character header limit, show the first name plus `+N`; an empty picker keeps the split pane with centered `No people to message` and no illustration or cards |
| SRC-OWNER-015 | Product owner, 2026-10-21 send-feedback follow-up | When frozen names do not fit the sending subtitle, use first name plus `+N`; status presentation remains text-only without status icons |
| SRC-EXP-CAR-001 | Active experience draft; owner-decided for inbox | [`EXP-MESSAGE-CAROUSEL`](message-carousel-and-playback.md): red-button exit pauses playback and preserves audio position and sketch (`DEC-039`, `EXP-037`); same-session play heads survive this exit (`DEC-026`); return re-entry staging is delegated here |
| SRC-EXP-LOGIN-001 | Active experience draft; owner-decided for login | [`EXP-LOGIN`](endpoint-login.md): mute signs out and blanks the display; lower-slot `connecting` / `no Wi-Fi` presentation; hangout roster is server-owned |
| SRC-EXP-SKETCH-001 | Active experience; owner-decided canvas geometry | [`EXP-SKETCH-RECORD-PLAYBACK`](sketch-record-and-playback.md): 120×120 logical sketch; 240×240 record canvas and 80×240 color rail; audio is the sketch clock; audio-only messages have no sketch. This send experience closes the timer/indicator question for its record screen (`DEC-021`) |
| SRC-COM-SEND-001 | Proposed v2 communication contract; not approved | [Message delivery](../communication-contracts/message-delivery.md): `accepted` means server archive commit, not notification or playback; atomic frozen-set send, keep/resume, recipients-only publication (`DEC-041`), and refusal-vs-wait classes (`DEC-042`) are recorded. Remaining work is routes, codecs, chunking, retention, and HTTP mapping of those classes |
| SRC-V1-SEND-001 | Active v1 assessment; implementation evidence only | [Recording and send](../../v1-assessments/recording-send.md): picker, capture, finishing/sending/sent, cancel, partial multi-recipient, discard-on-fail |
| SRC-UI-SPLIT-001 | Active device-UI draft | [`LAYOUT-SPLIT-PANE`](../device-ui/layouts/LAYOUT-SPLIT-PANE.md); [`PANEL-HEADER-TITLE-SUBTITLE`](../device-ui/panels/PANEL-HEADER-TITLE-SUBTITLE.md) for the sending sequence header |
| SRC-LAB-10-001 | Lab motion reference; owner-adopted for this experience | `lab.lab10` physical-button ring-wipe enter and reverse red-circle collapse. Pixel timing and clip geometry are delegated to match that lab, not reinvented |
| SRC-STD-001 | Normative engineering standard, not product policy | [Client application coding standards](../../standards/client-application-coding-standards.md) §§3–5, 7, 9: frozen inputs, one terminal outcome, truthful success, explicit discard |
| SRC-OPS-001 | Supporting concept guide | [Operational contract shape](../../v1-assessments/operational-contract-shape.md); [async operation envelope](../../v1-assessments/async-operation-envelope.md) |

**Source conflicts and applicability (reconstruction-confirmed):**

1. This brief and `EXP-MESSAGE-CAROUSEL` `DEC-039` agree: red button from the
   inbox pauses playback, keeps audio position and sketch progress, and
   enters this experience. That entry rule is already owner-decided there.
2. “Sent” means the server accepted the message for the selected recipients.
   It does **not** mean a recipient has played it. This agrees with the
   proposed delivery contract and is owner intent in this brief.
3. V1 used a long-press multi-select picker, an Everyone card, a 10-second
   picker timeout that was not implemented, `Finishing` / `Sending...` /
   `Sent` copy, an approximately two-second receipt, discard-on-fail, and
   per-recipient uploads that could partially succeed. The owner rejected
   Everyone and long-press (`DEC-013`), adopted a 2-second confirmation
   dwell (`DEC-019`), and replaced v1 body copy with lowercase `preparing`
   / `sending` / `sent` (`DEC-020`). V1 discard-on-fail is replaced by
   keep/resume for connectivity (`DEC-029`) and by atomic `couldn't send`
   for definite refusals (`DEC-034`, `DEC-042`). V1 partial multi-recipient
   upload is replaced by all-or-nothing (`DEC-034`). Legacy v1 leading-audio
   clipping is historical evidence for `DEC-017`, not a copied clip length.
4. `EXP-SKETCH-RECORD-PLAYBACK` locks canvas geometry and multi-color
   strokes. This send experience owns capture+send behavior and decides
   there is no on-screen timer or recording indicator (`DEC-021`). It does
   not redesign that canvas.
5. The owner adopted the `lab.lab10` physical-button ring-wipe and reverse
   red-circle collapse as this experience's recording enter/finish motion
   (`DEC-011`). The lab's on-screen FAB bloom is not the product enter path.
   Exact radii, origin, duration, and easing are delegated to engineering
   with the invariant that the motion matches that lab ring-wipe.
6. Delivery atomic frozen-set send (`DEC-034`), keep/resume (`DEC-029`–
   `DEC-035`), recipients-only publication (`DEC-041`), and refusal-vs-wait
   classes (`DEC-042`) are owner-decided. Wire routes, codecs, chunking,
   receipt retention, and exact HTTP status mapping of those classes stay
   engineering.

### Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-21 | Reconstruction draft from the owner's recording and sending brief. |
| 0.2 | 2026-10-21 | Reconstruction checkpoint confirmed. Locked this as the inbox send journey, inherited mute/connectivity/sketch, and adopted the `lab.lab10` ring-wipe enter and reverse collapse. |
| 0.3 | 2026-10-21 | Locked tap-to-toggle recipient selection, header copy, exclusion of self, and empty-recipient copy. |
| 0.4 | 2026-10-21 | Locked wipe-end as the start of the sent message, non-interactive sketch during the wipe, reverse collapse back to split pane with a stable `sending to ...` header, 2-second confirmation dwell, and return to the previous inbox selection. |
| 0.5 | 2026-10-21 | Locked send body copy, no record timer/indicator, 3-minute auto-finish, and no silence auto-stop. |
| 0.6 | 2026-10-21 | Locked red/Boot during wipe, preparing, sending, and confirmation. Ordinary send controls complete. |
| 0.7 | 2026-10-21 | Confirmed ordinary-flow checkpoint. Locked offline selection/recording and sending-wait presentation. Send retention and send timing remain OPEN. |
| 0.8 | 2026-10-21 | Locked keep-until-accepted, auto-send, Boot-leave without discard during connectivity wait, and one pending send at a time. |
| 0.9 | 2026-10-21 | Locked mute discard of unfinished capture, keep-and-resume of a finished send after re-login, and atomic `couldn't send`. |
| 1.0 | 2026-10-21 | Locked power-loss, hangout-member removal during selection, and full-sketch ignore-further-ink. |
| 1.1 | 2026-10-21 | Locked `couldn't record`, mid-recording mic-fail-as-finish, and exception-checkpoint delegated set. |
| 1.2 | 2026-10-21 | Contract checkpoint confirmed. No implementation authorization. |
| 1.3 | 2026-10-21 | Delivery contract rev 0.2 recorded atomic send and keep/resume. Remaining product meanings are refusal-vs-wait classification and sender inbox copy. |
| 1.4 | 2026-10-21 | Locked recipients-only publication and the refusal-vs-wait map. No implementation authorization. |
| 1.5 | 2026-10-21 | Locked recipient-picker selected-card cue, long-name header treatment, and empty-state composition. |
| 1.6 | 2026-10-21 | Locked sending-subtitle overflow and text-only status marks. |

## 2. Scenario and intended experience

### Original owner input

The owner supplied the following brief on 2026-10-21. It is preserved here as
the starting product statement, not as an approved specification.

> Define the experience of creating and sending a new audio message with an
> optional sketch, beginning from the message inbox and ending with a return
> to that inbox.
>
> Reuse existing Family Link UI patterns, particularly the split-panel
> layout and carousel navigation.
>
> **Entering the experience.** The user is viewing the message inbox.
> Pressing the physical red recording button pauses any active message
> playback, preserving its audio position and sketch progress. The device
> transitions into recipient selection.
>
> **Recipient selection.** Reuse the existing carousel component to display
> available recipients. Preserve the established upper-third header and
> lower-two-thirds carousel layout. Allow the user to select one or more
> message recipients. The physical red recording button advances to the
> recording/sketching screen once recipient selection is valid. The physical
> Boot button returns to the message inbox without creating a message.
>
> **Recording and sketching.** Transition from recipient selection into a
> full-screen recording/sketching experience. Use the special transition
> developed in `lab.lab10`. Audio recording and sketch creation occur
> together. The physical red recording button finishes the recording. The
> physical Boot button cancels recording and discards the unfinished
> message, returning to the inbox. Recording and sketching UI details remain
> open. Do not invent or replace the existing lab transition before its
> reference is supplied.
>
> **Preparing, sending, and sent confirmation.** When the user finishes,
> proceed through one continuous delivery sequence: prepare the captured
> audio and sketch; use the reverse `lab.lab10` transition in which the red
> circle collapses; show sending progress; show a brief sent confirmation
> that the server accepted the message for the selected recipients; then
> automatically return to the message inbox. Do not imply recipient playback
> or delivery acknowledgment merely because the server accepted the message.
>
> **Navigation and cancellation.** Physical controls are contextual. During
> preparing/sending and sent confirmation, red-button and Boot behavior are
> to be determined. Cancellation must not accidentally send a partial
> message.
>
> **Connectivity and recovery.** Reuse established `connecting` and
> `no Wi-Fi` conventions. Do not automatically assume that recording must
> stop when the network is unavailable. Preserve agreed session, retry, and
> recovery requirements from other experiences.

### Reconstruction (confirmed 2026-10-21)

- **Person:** A hangout member who is already signed in at a shared desk
  endpoint. Example names used later (Maya, Jordan, Sam) are illustrations,
  not product identities.
- **Setting:** The same always-on Family Link endpoint used for login and
  the inbox: 320 × 240 landscape touch display, physical off-screen red
  recording button, physical Boot button, and physical mute latch. English
  only.
- **Goal:** Choose who should receive a new message, record audio, optionally
  draw, and know the server has accepted that message. Then get back to the
  inbox.
- **Start:** The person is on the signed-in message inbox
  (`EXP-MESSAGE-CAROUSEL`), which may be empty, showing cards, playing, or
  showing connectivity status. They press the red recording button.
- **Immediate effect of start:** Any active inbox playback pauses. That
  message's audio position and sketch progress are kept. The person stays
  signed in. The device enters recipient selection.
- **Ordinary flow:** The person sees other hangout members in a split-panel
  carousel, not themselves. Nobody is pre-selected. They swipe to browse
  and tap cards to toggle who will receive the message. The header names
  the current recipients. With at least one selected, the red button
  starts the ring-wipe. Sketching waits until the wipe ends; that is also
  the start of the sent audio. They talk and may draw, then finish with
  red. The reverse collapse returns to the split pane with `sending to ...`
  and those names. The body moves through preparing, sending, and
  confirmation, then the previous inbox selection returns after 2 seconds.
- **Completion:** The experience ends when the inbox is showing again after
  a successful send, or after a cancel that discarded the unfinished
  message. Success means the server accepted the message for the selected
  recipients. It does not mean anyone has heard it.
- **Intended qualities, translated only as far as the brief allows:** The
  send path reuses inbox carousel and split-panel language where it is
  still a selection surface. Capture is a different, full-screen activity.
  Preparing, sending, and sent are one continuous sequence, not screens the
  person pages through. Cancel never submits a partial message.

## 3. Physical and interaction context

- Display and inbox/picker shell:
  [`LAYOUT-SPLIT-PANE`](../device-ui/layouts/LAYOUT-SPLIT-PANE.md) — upper
  80 px, lower 160 px. Recipient selection keeps this ⅓ / ⅔ split.
- Recipient cards reuse the established carousel. Tap toggles membership in
  the recipient set. Swipe browses and may change which card is centered
  without changing who is selected. Selected chrome is distinct from the
  centered/focused card. Exact selected styling is delegated to device UI.
- Record/sketch surface: full-canvas. Steady-state geometry is inherited
  from `EXP-SKETCH-RECORD-PLAYBACK` (240×240 canvas, 80×240 color rail).
  The canvas and color rail are not interactable until the ring-wipe
  finishes (`DEC-016`). There is no on-screen recording timer or recording
  indicator (`DEC-021`).
- Physical red recording button (`DEC-023`–`DEC-025`):

  | Stage | Red |
  |---|---|
  | Message inbox | If a send is still pending, return to that wait screen; otherwise enter recipient selection |
  | Recipient selection | Begin ring-wipe when selection is valid; otherwise ignore |
  | Ring-wipe | Ignore |
  | Recording / sketching | Finish and begin preparing |
  | Preparing / sending (accepted path) | Ignore |
  | Preparing / sending (`no Wi-Fi` / `connecting` wait) | Ignore |
  | `sent` confirmation | Ignore |

- Physical Boot button:

  | Stage | Boot |
  |---|---|
  | Message inbox | Existing inbox behavior (settings) |
  | Recipient selection | Return to inbox; no message |
  | Ring-wipe | Cancel, discard, return to inbox |
  | Recording / sketching | Cancel, discard, return to inbox |
  | Preparing / sending (accepted path) | Ignore |
  | Preparing / sending (`no Wi-Fi` / `connecting` wait) | Return to inbox; do not discard the queued send |
  | `sent` confirmation | Ignore |
- Physical mute: inherited from `EXP-LOGIN`. Mute blanks the display and
  signs the user out. Unmute returns to signed-out login, not this
  experience. Unfinished recording is discarded (`DEC-032`). A finished
  send is kept and resumes only after that same user signs in (`DEC-033`).
  Power loss discards unfinished capture. A finished send is kept across
  restart if local storage survives, then resumes when that user signs in
  (`DEC-035`).
- Connectivity presentation (`DEC-026`–`DEC-028`): split-panel stages reuse
  `connecting` with three progressive dots, or `no Wi-Fi`, in the lower
  slot. Recipient selection keeps a known member list through an outage.
  Full-canvas recording is not covered by those messages. During
  preparing/sending wait, those messages replace the phase body while the
  header stays `sending to ...`.
- Recording enter and finish motion (`DEC-011`, `DEC-016`, `DEC-018`): use
  the `lab.lab10` physical-button **ring-wipe**. A red ring grows from below
  the screen and reveals the full-canvas record surface. Sketching is not
  possible until that wipe completes. When the person finishes recording,
  the reverse collapse returns to the split-pane layout. The lab's
  on-screen FAB bloom is not used. Exact origin, radii, duration, easing,
  and clip geometry are delegated to engineering provided the visible
  motion matches that lab ring-wipe.

## 4. Experience model and ordinary journey

Ordinary send path locked, including physical controls. Connectivity
presentation, keep/resume, mute-during-send, atomic refusal, recipients-
only publication, and refusal-versus-wait classification are locked.

| Stage | Purpose | Entry | Exit | Surviving work |
|---|---|---|---|---|
| Recipient selection | Choose one or more other hangout members | Red button from inbox | Valid selection + red button → recording wipe; Boot → inbox, no message; mute | Selected recipient identities (frozen when the wipe starts); paused inbox play head remains owned by the inbox experience |
| Empty recipients | No one available to message | Red button from inbox when no other hangout members | Boot → inbox; a member becomes available; mute | No selection; no message |
| Recording enter (wipe) | Reveal the record surface | Valid recipients + red button | Wipe completes → interactive recording; Boot → inbox, discard; mute | Frozen recipient set; hardware capture may already be running and must be discarded on Boot |
| Recording / sketching | Capture the sent audio and optional sketch together | Wipe completes | Red, or 3-minute cap → reverse collapse into preparing; Boot → inbox, discard; mute | Frozen recipient set; in-progress audio and sketch from wipe-complete onward |
| Preparing / sending / sent | One continuous delivery sequence on the split pane | Red or duration cap finishes recording | `sent` then inbox; Boot from connectivity wait → inbox with send still pending; red from inbox while pending → this wait; failure TBD | Frozen recipients, captured media, one send operation until accepted |
| Message inbox (exit) | Return to `EXP-MESSAGE-CAROUSEL` | Successful sent confirmation, or cancel from selection or recording | Owned by the inbox experience | Previous inbox selection and same-session play heads, if still signed in |

```text
Inbox --red--> Recipient selection --red (valid)--> Wipe --complete--> Recording
                     | Boot                  | Boot                      | Boot
                     v                       v                           v
                   Inbox <----- auto after sent ---- Preparing/sending/sent
                                                     (red/Boot ignored)
```

The `lab.lab10` ring-wipe attaches to recording enter. The reverse
red-circle collapse returns to the split pane for preparing/sending/sent
(`DEC-011`, `DEC-018`).

### Ordinary recipient selection (accepted 2026-10-21)

Maya is signed in. She presses red from the inbox. Playback pauses if it was
running. Recipient selection shows other hangout members in the lower
carousel. The header is `Send a message to ...`. Nobody is selected. Maya
is not in the list.

She swipes past Sam to Jordan and taps Jordan. Jordan is selected. The
header becomes `To Jordan`. She swipes back to Sam and taps Sam. Both stay
selected. The header names both. Tapping Jordan again removes only Jordan.
Swipe never toggles selection by itself.

With at least one person selected, red starts the ring-wipe. With none
selected, red does nothing. Boot from this stage returns to the inbox and
creates no message.

If the hangout has no one else, the lower slot shows `No people to message`.
Red is ignored. Boot still returns to the inbox.

### Ordinary recording and send (accepted 2026-10-21)

The canvas and color rail ignore touches until the wipe finishes. Speech
during the wipe is not part of the sent message. When the wipe ends, Maya
can talk and draw. That is the start of the message that will be sent.

She talks and may draw. There is no timer or recording indicator. If she
never draws, the message is still audio-only. Red finishes, or recording
auto-finishes at 3 minutes the same way. Silence does not stop recording.

The reverse collapse returns to the split pane. The header uses
[`PANEL-HEADER-TITLE-SUBTITLE`](../device-ui/panels/PANEL-HEADER-TITLE-SUBTITLE.md)
and stays stable through preparing, sending, and confirmation: title
`sending to ...`, subtitle the same recipient names she had when recording
started. The lower body replaces the recipient carousel with centered
status copy: `preparing` and three progressive dots, then `sending` and
three progressive dots, then `sent` with no dots. After `sent` has been
visible for 2 seconds, the inbox returns on the message she had selected
before, still paused, with no autoplay. Red and Boot do nothing during
preparing, sending, and `sent`. Boot during the wipe discards and returns
to the inbox; a second red during the wipe is ignored.

If Wi-Fi drops while Jordan is already selected, the carousel and
selection stay. Red may still start recording. The lower slot shows
`connecting` or `no Wi-Fi` only when no hangout member list is known.
If Wi-Fi drops while Maya is recording, recording continues; the canvas
is not replaced by connectivity copy. If she has already finished and the
body would say `sending`, the header stays `sending to ...` and the body
shows `no Wi-Fi` or `connecting` instead of `sent`. The finished message
is kept until this same send is accepted, and it is submitted
automatically when the server can accept it. Boot from that wait returns
to the inbox without discarding. If she already left, skip `sent`. Red
from the inbox while it is still pending returns her to this wait
screen. Mute during recording discards. Mute after finish keeps the send
and resumes it when Maya signs in again. A definite server refusal shows
`couldn't send`, then discards that attempt. Timeouts, 5xx, and an expired
session keep waiting. Unsupported or too-large media, and a
`client_message_id` content conflict, are `couldn't send`. The accepted
message is published only to the frozen recipients; Maya's inbox does not
gain a card of her own send. Power loss drops unfinished
recording. A finished queued send survives restart if storage does, then
resumes when Maya signs in. If Jordan leaves the hangout before she
presses red, he is dropped from selection. A full sketch stops taking ink
and keeps recording audio. If the microphone is not available when she
presses red, she stays on recipient selection after `couldn't record`. If
the microphone fails after recording has started, that finish is sent.

## 5. Stage and operation contracts

### Recipient selection

- Purpose: choose who will receive the new message.
- Entry: physical red recording button from the signed-in inbox (`DEC-002`).
- Preconditions: signed in. The hangout roster may be empty of other members.
- Inputs: current hangout members except the signed-in user (`DEC-015`).
- Immediate feedback: split-panel carousel; header `Send a message to ...`;
  no recipient pre-selected (`DEC-013`, `DEC-014`).
- Accepted actions: swipe to browse; tap to toggle a card in or out of the
  recipient set; Boot to inbox with no message; mute. Red begins recording
  only when at least one recipient is selected.
- Disabled / ignored: red while none are selected; red while `No people to
  message` is showing.
- Success: at least one selected recipient and red → ring-wipe, with that
  recipient set frozen at that press, unless capture cannot start
  (`DEC-038`).
- Capture cannot start (`DEC-038`): keep this stage and the current
  selection. Show `couldn't record` in the lower slot for 2 seconds, then
  restore the carousel. If a wipe had started, reverse it back here first.
  Nothing is sent. Red is ignored during those 2 seconds. Boot still
  returns to the inbox.
- Connectivity (`DEC-026`): if a hangout member list is already known, keep
  the carousel and current selection through Wi-Fi or server loss. Red may
  start recording while offline. Show `connecting` or `no Wi-Fi` in the
  lower slot only when no member list is known. Boot still returns to the
  inbox.
- Roster churn (`DEC-036`): if a selected person leaves the hangout before
  the recipient set is frozen, remove them from the carousel and from the
  selection. If none remain selected, header returns to
  `Send a message to ...` and red is ignored. Still-selected people stay
  selected. Newly appearing members are shown and are not selected.
- Engineering assumption: local capture and storage while offline is
  possible. If engineering later finds that infeasible, return the decision
  to the owner. Do not silently change this behavior.
- Never acceptable: recording starts with an empty recipient set; the
  signed-in user is offered as a recipient; swipe silently changes who will
  receive the message; an Everyone card appears.

### Recording enter (ring-wipe)

- Purpose: reveal the full-canvas record surface.
- Entry: valid recipient selection + red.
- Immediate feedback: `lab.lab10` physical ring-wipe. Canvas and color rail
  are visible as revealed but not interactable (`DEC-016`).
- User-visible recording start: wipe complete. That instant is the start of
  the sent audio and of any sketch (`DEC-016`).
- Hardware capture: MAY begin during the wipe when the device can run the
  animation and capture together, so leading hardware noise can be clipped.
  Engineering MUST clip leading audio so kept audio starts at wipe-complete.
  If overlap is not affordable, start capture at wipe-complete and still
  clip any abrupt hardware onset. Clip duration is measured, not a copied
  legacy constant (`DEC-017`).
- Ignored: red during the wipe (`DEC-023`).
- Cancel: Boot discards any capture, creates no message, and returns to
  the inbox (`DEC-023`). Interrupted-wipe motion is delegated provided that
  outcome holds.
- Capture cannot start (`DEC-038`): reverse the wipe back to recipient
  selection, then show `couldn't record`. Nothing is sent. Recipients stay
  selected.
- Never acceptable: sketching during the wipe; sending wipe-period speech
  as the message; sending the abrupt hardware-start sound as the start of
  the message; a second red finishing an empty recording; a recording
  without audio because capture never started.

### Recording / sketching

- Purpose: capture audio and an optional sketch together after the canvas
  is usable.
- Entry: ring-wipe complete.
- Accepted actions: draw on the canvas; choose rail colors; red finishes;
  Boot cancels and discards; mute. No on-screen timer or recording
  indicator (`DEC-021`).
- Success: red, reaching 3 minutes, or microphone failure after
  wipe-complete → reverse collapse to the split-pane sending sequence
  (`DEC-022`, `DEC-039`).
- Not an end condition: silence. Drawing with little or no speech must not
  stop or discard the recording.
- Full sketch (`DEC-037`): if the sketch buffer cannot accept more ink,
  keep recording audio, ignore further drawing, and do not auto-finish.
  Red, Boot, mute, and the 3-minute cap still apply. The cap value is
  delegated to engineering.
- Connectivity (`DEC-027`): connectivity loss does not stop recording and
  does not cover the canvas with `connecting` or `no Wi-Fi`. Red still
  finishes. Boot still cancels.
- Cancel: Boot discards the unfinished message and returns to the inbox.
  Mute discards the unfinished message and signs out (`DEC-032`).
- Never acceptable: a cancelled recording is uploaded; connectivity copy
  interrupts an active canvas; mute uploads an unfinished recording.

### Preparing / sending / sent

- Purpose: one continuous delivery sequence after finish.
- Entry: red finishes recording. Reverse collapse returns to the split pane
  (`DEC-018`).
- Header: [`PANEL-HEADER-TITLE-SUBTITLE`](../device-ui/panels/PANEL-HEADER-TITLE-SUBTITLE.md).
  Title is `sending to ...`. Subtitle is the same recipient names frozen
  when recording started. The header does not change across preparing,
  sending, and confirmation.
- Body: replaces the recipient carousel with the established centered
  status presentation. Copy is `preparing` with three progressive dots,
  then `sending` with three progressive dots, then `sent` with no dots
  (`DEC-020`). No overlay and no dim.
- Success: server accepts the message for those recipients; `sent` is
  shown for 2 seconds; then return to the inbox (`DEC-019`).
- Ignored: red during preparing, sending, wait, and `sent`. Boot during
  `preparing`, `sending`, and `sent` (`DEC-024`, `DEC-025`).
- Leave without discard: Boot while the body shows `no Wi-Fi` or
  `connecting` after finish returns to the inbox and keeps this send
  (`DEC-031`).
- One pending send: red from the inbox while this send is not yet accepted
  returns to the wait screen and does not start another recording
  (`DEC-031`).
- Inbox return: the previously selected inbox message, still paused, no
  autoplay. Do not select the just-sent message. The sender's inbox MUST
  NOT gain a card of this send (`DEC-041`).
- Never acceptable: `sent` without server acceptance; presenting `sent` as
  playback or notification proof; showing `sent` after a refusal; delivering
  to only some of a frozen recipient set; uploading while signed out;
  publishing a copy into the sender's inbox.

Connectivity wait (`DEC-028`–`DEC-031`): if Wi-Fi is unavailable, the body
shows `no Wi-Fi`. If Wi-Fi is up but the server has not accepted this
send, the body shows `connecting` with three dots. The header does not
change. Do not show `sent`. This is the same send operation.

Keep the finished audio, optional sketch, and frozen recipients until the
server accepts this send. Do not discard because the network is down.
Submit automatically as soon as Wi-Fi is up and the server can accept;
do not require another red press. Body then returns to `sending`, then
`sent` if she is still on this screen.

Boot from `no Wi-Fi` / `connecting` wait returns to the inbox and does
not discard. If acceptance happens after she left, skip the 2-second
`sent` confirmation and do not interrupt the inbox.

Mute after finish keeps the queued send. Do not upload while signed out.
When that same user signs in again, resume this send automatically. After
login, enter the inbox as usual; red returns to the wait screen if it is
still pending. Skip `sent` if it was accepted while she was away
(`DEC-033`). A pending send belongs to that sender. It must not run as a
different signed-in user.

Definite refusal (`DEC-034`, `DEC-042`): if the server refuses this send
for a non-connectivity reason, nobody in the recipient set receives it.
Body shows `couldn't send` with no dots. Header stays `sending to ...`.
After 2 seconds, return to the inbox and discard that attempt. Do not
auto-retry a refusal. Do not show `sent`.

Classified as definite refusal: frozen recipient is not a current hangout
member; unsupported or too-large media; same `client_message_id` with
different content. Classified as wait, not refusal: no Wi-Fi, timeout,
connection reset, HTTP 5xx, expired or forbidden session, and a malformed
or unknown-schema success body. Wait keeps the queued send and never
shows `sent`. An expired session does not upload until this same user
has a valid session again.

Power loss (`DEC-035`): unfinished capture is gone. A finished queued send
is kept across restart if local storage survives, then resumes when that
same user signs in, same as mute-after-finish. If storage did not
survive, there is no send and no recovery UI.

Known never-acceptable outcomes already in force:

- A cancelled or unfinished recording is uploaded or marked sent.
- `sent` is shown when the server has not accepted the message, including
  after a refusal.
- `sent` is presented as proof that a recipient played or was notified.
- Only some of a frozen recipient set receive the message.
- Recording begins for a recipient set the person did not confirm.
- Sketching is possible before the ring-wipe completes.
- Wipe-period speech or hardware-start noise is kept as the start of the
  sent message.
- Implementation substitutes a different enter/finish motion for the
  `lab.lab10` physical ring-wipe.
- The sender's inbox gains a card of this send.
- A timeout, 5xx, or expired session is shown as `couldn't send`.

## 6. Continuity, data, and side effects

Inherited facts that this experience must not contradict:

- Inbox play heads and partial sketches from the current signed-in session
  survive this exit (`EXP-MESSAGE-CAROUSEL` `DEC-026`, `DEC-039`).
- After a successful send, this experience restores that same inbox
  selection, still paused, without autoplay (`DEC-019`).
- Mute ends the signed-in session (`EXP-LOGIN`). Unfinished capture is
  discarded. A finished send is kept for that user and resumes after they
  sign in (`DEC-032`, `DEC-033`).
- Server acceptance is distinct from recipient notification and playback
  (`COM-MESSAGE-DELIVERY`). Acceptance publishes only to frozen recipients;
  it does not add a card to the sender's inbox (`DEC-041`).
- The sent audio/sketch timeline starts when the ring-wipe completes. Sketch
  timestamps must align with that kept audio (`DEC-016`, `DEC-017`).

## 7. Limits and responsiveness

- Sent confirmation remains visible for **2 seconds**, then the inbox
  returns (`DEC-019`).
- Recording auto-finishes at **3 minutes** (180 seconds) from wipe-complete,
  using the same finish path as red (`DEC-022`).
- Silence does not end recording (`DEC-022`).
- Leading-audio clip duration is measured from hardware onset behavior. It
  is not a product-chosen number and must not be copied from legacy v1.
- Sketch buffer capacity is an engineering limit. When it is reached,
  further ink is ignored and audio continues (`DEC-037`).
- Stuck-send guard is not a separate product timeout; connectivity wait
  retries until acceptance, mute, power-loss without storage, or a
  definite refusal.

## 8. Acceptance scenarios and evidence

| ID | Starting condition | Action or event | Expected result | Requirements |
|---|---|---|---|---|
| SCN-001 | Signed in; Jordan's message is playing | Press red | Playback pauses; Jordan's position and sketch are kept; recipient selection appears; Maya stays signed in | EXP-001, EXP-002 |
| SCN-002 | Recipient selection; nobody selected | Press red | Remain on recipient selection; recording does not start | EXP-008 |
| SCN-003 | Recipient selection; Jordan and Sam visible | Tap Jordan, then tap Sam | Both stay selected; header names both | EXP-005, EXP-007 |
| SCN-004 | Jordan and Sam selected; Jordan centered | Swipe to Sam without tapping | Selection still Jordan and Sam; only the centered card changes | EXP-005, EXP-006 |
| SCN-005 | Jordan selected | Tap Jordan again | Jordan is not selected; header returns to `Send a message to ...` if none remain | EXP-005, EXP-007 |
| SCN-006 | At least one recipient selected | Press red | Ring-wipe begins; recipients freeze; canvas is not yet interactable | EXP-008, EXP-011, EXP-016 |
| SCN-010 | Ring-wipe is running; Maya talks and tries to draw | Wipe in progress | Touches do nothing; that speech is not in the sent message | EXP-016, EXP-017 |
| SCN-011 | Wipe completes | Maya talks and draws, then presses red | Audio and sketch from wipe-complete onward are the message; reverse collapse returns to split pane | EXP-016, EXP-012, EXP-018 |
| SCN-012 | Recording finished; Jordan and Sam were selected | Collapse to split pane | Header title `sending to ...`; subtitle those names; body shows `preparing` with three dots, then `sending` with three dots, then `sent` with no dots | EXP-018, EXP-020 |
| SCN-013 | Server has accepted the message | `sent` showing | After 2 seconds, inbox shows the previously selected message, still paused, no autoplay; the just-sent message is not selected | EXP-019 |
| SCN-014 | Interactive recording; Maya never touches the canvas | Press red | Audio-only message prepares and sends; no sketch | EXP-012, EXP-021 |
| SCN-015 | Interactive recording | Observe the record screen | Canvas and color rail only; no timer; no recording indicator | EXP-021 |
| SCN-016 | Maya has been recording for 3 minutes from wipe-complete | Duration cap | Same finish path as red: reverse collapse, then preparing/sending/sent | EXP-022 |
| SCN-017 | Maya draws for a long time with little speech | Silence | Recording continues until red, Boot, mute, or the 3-minute cap | EXP-022 |
| SCN-018 | Ring-wipe is running | Press Boot | Capture discarded; inbox returns; no message sent | EXP-023 |
| SCN-019 | Ring-wipe is running | Press red | Wipe continues; recording does not finish | EXP-023 |
| SCN-020 | Body shows `preparing` or `sending` | Press red or Boot | Phase continues; no new recording; no cancel | EXP-024 |
| SCN-021 | Body shows `sent` | Press red or Boot | Wait out the 2 seconds; then inbox as EXP-019 | EXP-025 |
| SCN-022 | Jordan selected; Wi-Fi drops; member list is known | Observe, then press red | Carousel and selection stay; recording can start | EXP-026 |
| SCN-023 | No known hangout members; Wi-Fi down | Open recipient selection | Lower slot `no Wi-Fi`; red ignored; Boot returns to inbox | EXP-026 |
| SCN-024 | Maya is drawing; Wi-Fi drops | Continue recording | Canvas stays; no connectivity copy; red finishes; Boot cancels | EXP-027 |
| SCN-025 | Body would show `sending`; Wi-Fi drops | Observe | Header stays `sending to ...`; body `no Wi-Fi`; not `sent`; finished work is kept | EXP-028, EXP-029 |
| SCN-026 | Finished send waiting on `no Wi-Fi` | Wi-Fi returns and the server accepts | Body `connecting` then `sending` then `sent` if still on this screen; same send, no second message | EXP-030 |
| SCN-027 | Body shows `no Wi-Fi` after finish | Press Boot | Inbox returns; queued send is kept and continues | EXP-031 |
| SCN-028 | Inbox; a send is still pending | Press red | Return to the wait screen; do not start recipient selection | EXP-031 |
| SCN-029 | She Booted to the inbox; the pending send is then accepted | Server accepts | Inbox is not interrupted; skip `sent` | EXP-031 |
| SCN-030 | Interactive recording | Mute on | Unfinished capture discarded; display off; signed out | EXP-032 |
| SCN-031 | Finished send waiting on `no Wi-Fi` | Mute on, later Maya signs in | Send kept; no upload while signed out; same send resumes after login; inbox first; skip `sent` if accepted while away | EXP-033 |
| SCN-032 | Finished send waiting; Sam then signs in | Sam uses the endpoint | Maya's pending send does not send as Sam and is not shown to Sam | EXP-033 |
| SCN-033 | Server refuses the send (not connectivity) | Refusal | Body `couldn't send`; header `sending to ...`; after 2 seconds inbox; attempt discarded; no `sent`; no recipient received it | EXP-034 |
| SCN-034 | Interactive recording | Power lost | Unfinished capture gone; no queued send | EXP-035 |
| SCN-035 | Finished send queued; power lost; storage survived; Maya signs in | Restart then login | Same send resumes after sign-in; inbox first | EXP-035 |
| SCN-036 | Finished send queued; power lost; storage did not survive | Restart | No send; no recovery UI | EXP-035 |
| SCN-037 | Jordan selected; Jordan leaves the hangout before red | Roster update | Jordan removed from carousel and selection; if none remain, `Send a message to ...` and red ignored | EXP-036 |
| SCN-038 | Jordan and Sam selected; Jordan leaves | Roster update | Sam stays selected; header names Sam | EXP-036 |
| SCN-039 | Sketch buffer cannot accept more ink; Maya is still talking | Further drawing | Audio continues; new ink ignored; recording does not auto-finish | EXP-037 |
| SCN-040 | Jordan selected; microphone unavailable | Press red | `couldn't record` for 2 seconds; carousel restores with Jordan still selected; nothing sent | EXP-038 |
| SCN-041 | Wipe already started; capture cannot start | Capture failure | Reverse to recipient selection, then SCN-040 | EXP-038 |
| SCN-042 | Interactive recording; microphone fails | Mic failure | Same finish path as red: reverse collapse, then preparing/sending | EXP-039 |
| SCN-043 | Server accepts Maya's send to Jordan; Maya returns to inbox | Observe Maya's inbox | Previous selection restored; no new card of Maya's own send | EXP-019, EXP-040 |
| SCN-044 | Upload result is unsupported or too-large media | Refusal | Body `couldn't send`; attempt discarded; nobody received it | EXP-034, EXP-041 |
| SCN-045 | Complete times out or the server returns 5xx | Wait | Body `connecting` or `no Wi-Fi`; same send kept; not `sent` | EXP-028, EXP-041 |
| SCN-046 | Same `client_message_id` replayed with different content | Identity conflict | `couldn't send`; no second delivery; attempt discarded | EXP-034, EXP-041 |
| SCN-047 | Session expired or forbidden while Maya is still signed in locally | Auth failure | Keep queued; do not upload until Maya has a valid session; not `sent` | EXP-033, EXP-041 |
| SCN-048 | Multiple recipients selected; their names exceed the 25-character `To` header limit | Observe header | Header shows the first selected recipient's first name and `+N` for the remaining recipients | EXP-007 |
| SCN-049 | A recipient is selected | Observe that person's card | Selected state is shown by the 2 px accent outline alone; no checkmark appears | EXP-006 |
| SCN-050 | Hangout has no other members | Open recipient selection | Split pane remains; lower slot centers `No people to message` with no illustration or cards | EXP-010 |
| SCN-051 | Frozen recipient names do not fit in the sending subtitle | Observe preparing/sending/sent | Subtitle shows the first recipient's first name and `+N` for the remaining recipients | EXP-042 |
| SCN-052 | Observe preparing/sending/sent and refusal feedback | Status changes | Centered text appears without icons; only `preparing` and `sending` have progressive dots | EXP-020, EXP-043 |
| SCN-007 | Recipient selection, any selection | Press Boot | Return to inbox; no message created | EXP-009 |
| SCN-008 | Maya is the signed-in user | Open recipient selection | Maya's card is not in the carousel | EXP-004 |
| SCN-009 | Hangout has no other members | Open recipient selection | Lower slot shows `No people to message`; red is ignored; Boot returns to inbox | EXP-010 |

Traceability for send, failure, connectivity, sender-copy, and
refusal-versus-wait scenarios is in the table below.

### Requirements issued so far

- **EXP-001:** This experience starts when a signed-in user presses the
  physical red recording button on the message inbox and ends when they
  return to that inbox after send or cancel. (Owner-decided)
- **EXP-002:** That press pauses any active inbox playback, preserves that
  message's audio position and sketch progress, keeps the user signed in,
  and enters recipient selection. (Owner-decided; inherits carousel
  `DEC-039`)
- **EXP-003:** Recipient selection uses the established split-panel
  carousel: upper-third header, lower-two-thirds cards. (Owner-decided)
- **EXP-004:** Available recipients are other hangout members. The
  signed-in user is not offered. There is no Everyone card. (Owner-decided)
- **EXP-005:** Tap a person card to toggle them in or out of the recipient
  set. Swipe browses and may change the centered card without changing
  selection. Nobody is pre-selected on entry. (Owner-decided)
- **EXP-006:** The centered/focused card is not the same as selection.
  Selected cards keep a persistent selected state while they remain
  selected, shown by a 2 px accent outline alone, without a checkmark.
  (Owner-decided)
- **EXP-007:** Header copy is `Send a message to ...` when none are
  selected, `To [name]` for one selected person, and `To` plus the selected
  names when more than one are selected. If selected names exceed the
  25-character header limit, show `To [first name] +N`, where `N` is the
  number of remaining selected recipients. (Owner-decided)
- **EXP-008:** Recipient selection is valid only when at least one person
  is selected. The red button is ignored until then. (Owner-decided)
- **EXP-009:** Boot from recipient selection returns to the message inbox
  and does not create a message. (Owner-decided)
- **EXP-010:** If there are no other hangout members, keep the split
  layout, show centered `No people to message` in the lower slot with no
  illustration or cards, ignore red, and still allow Boot to return to the
  inbox. (Owner-decided)
- **EXP-011:** When selection is valid, red starts the `lab.lab10` physical
  ring-wipe onto the inherited full-screen record canvas and freezes the
  recipient set. (Owner-decided)
- **EXP-012:** After the canvas is interactable, red finishes capture and
  begins the reverse red-circle collapse. Boot cancels, discards the
  unfinished message, and returns to the inbox. (Owner-decided)
- **EXP-013:** Preparing, sending, and confirmation are one continuous
  sequence on the split pane after the reverse collapse, then the inbox
  returns automatically. (Owner-decided)
- **EXP-014:** Confirmation of success means the server accepted the
  message for the selected recipients. It does not mean notification,
  playback, or delivery acknowledgment. (Owner-decided)
- **EXP-016:** The canvas and color rail are not interactable until the
  ring-wipe completes. That instant is the start of the sent audio and of
  any sketch. Speech during the wipe is not part of the sent message.
  (Owner-decided)
- **EXP-017:** Hardware capture MAY start during the wipe when the device
  can animate and capture together. Engineering clips leading audio so the
  kept recording starts at wipe-complete, including any abrupt hardware
  onset. If overlap is not affordable, capture starts at wipe-complete and
  still clips that onset. Clip duration is measured, not a legacy constant.
  Sketch timestamps align with the kept audio. (Owner-decided; clip
  duration delegated)
- **EXP-018:** The reverse collapse returns to the split pane. The sending
  header is [`PANEL-HEADER-TITLE-SUBTITLE`](../device-ui/panels/PANEL-HEADER-TITLE-SUBTITLE.md)
  and stays stable through preparing, sending, and confirmation: title
  `sending to ...`; subtitle the frozen recipient names, reduced to the
  first recipient's first name plus `+N` if they do not fit. The lower body
  replaces the recipient carousel with centered status copy (`EXP-020`).
  (Owner-decided)
- **EXP-019:** After server acceptance, keep `sent` visible for 2 seconds,
  then return to the inbox on the previously selected message, still
  paused, with no autoplay. Do not select the just-sent message.
  (Owner-decided)
- **EXP-020:** Lower-body copy is `preparing` with three progressive dots,
  then `sending` with three progressive dots, then `sent` with no dots. Use
  the established centered, text-only status presentation: no icons,
  overlay, or dim.
  (Owner-decided)
- **EXP-021:** The record screen shows only the inherited canvas and color
  rail. It MUST NOT show a recording timer or recording indicator.
  Audio-only messages (no sketch) are valid. (Owner-decided)
- **EXP-022:** Recording auto-finishes 3 minutes after wipe-complete, using
  the same finish path as red. Silence MUST NOT stop or discard recording.
  (Owner-decided)
- **EXP-023:** During the ring-wipe, Boot cancels, discards any capture,
  and returns to the inbox with no message. Red is ignored. (Owner-decided)
- **EXP-024:** During `preparing` and `sending`, red and Boot are ignored.
  Boot from the `no Wi-Fi` / `connecting` wait is EXP-031, not this rule.
  (Owner-decided)
- **EXP-025:** During `sent` confirmation, red and Boot are ignored. The
  2-second dwell and inbox return still apply. (Owner-decided)
- **EXP-026:** If a hangout member list is known, recipient selection keeps
  that list and any current selection through connectivity loss, and red
  may start recording. Show `connecting` or `no Wi-Fi` in the lower slot
  only when no member list is known. Boot still returns to the inbox.
  (Owner-decided; assumes local offline capture is possible)
- **EXP-027:** Connectivity loss during recording does not stop capture and
  MUST NOT replace the canvas with `connecting` or `no Wi-Fi`. Red still
  finishes. Boot still cancels. (Owner-decided)
- **EXP-028:** After finish, if this send cannot yet be accepted, keep the
  `sending to ...` header, show `no Wi-Fi` or `connecting` in the body, and
  do not show `sent`. This remains the same send, not a new message.
  (Owner-decided)
- **EXP-029:** Keep the finished audio, optional sketch, and frozen
  recipients until the server accepts this same send. Do not discard
  because the network is down. Do not create a second message for retry.
  (Owner-decided)
- **EXP-030:** Submit automatically as soon as Wi-Fi is up and the server
  can accept this send. Do not require another red press. If she is still
  on the wait screen, the body returns to `sending`, then `sent`.
  (Owner-decided; retry schedule delegated)
- **EXP-031:** While the body shows `no Wi-Fi` or `connecting` after
  finish, Boot returns to the inbox and does not discard the queued send.
  If acceptance happens after she left, skip `sent` and do not interrupt
  the inbox. Red from the inbox while this send is still pending returns
  to the wait screen and MUST NOT start another recording. (Owner-decided)
- **EXP-032:** Mute during recording or the ring-wipe discards unfinished
  capture, signs the user out, and blanks the display. Unmute is login, not
  this experience. (Owner-decided)
- **EXP-033:** Mute after finish keeps the queued send and MUST NOT upload
  while signed out. When that same user signs in again, resume this send
  automatically. Login still enters the inbox. Skip `sent` if accepted
  while away. A pending send MUST NOT run as a different signed-in user.
  (Owner-decided)
- **EXP-034:** If the server refuses this send for a non-connectivity
  reason, the recipient set is all-or-nothing: nobody receives it. Show
  `couldn't send` with no dots under `sending to ...` for 2 seconds, then
  return to the inbox and discard that attempt. Do not auto-retry a
  refusal. Do not show `sent`. (Owner-decided)
- **EXP-035:** Power loss discards unfinished capture. A finished queued
  send is kept across restart if local storage survives, then resumes when
  that same user signs in. If storage did not survive, there is no send and
  no recovery UI. (Owner-decided)
- **EXP-036:** If a selected person leaves the hangout before the recipient
  set is frozen, remove them from the carousel and from the selection. If
  none remain selected, show `Send a message to ...` and ignore red.
  Remaining selections stay. New members appear unselected. After freeze,
  a later membership change does not rewrite the frozen set; a server
  refusal uses EXP-034. (Owner-decided)
- **EXP-037:** If the sketch buffer cannot accept more ink, keep recording
  audio, ignore further drawing, and do not auto-finish. Red, Boot, mute,
  and the 3-minute cap still apply. (Owner-decided; cap size delegated)
- **EXP-038:** If capture cannot start, do not open a usable record screen.
  Keep recipient selection and the current selection. Show `couldn't record`
  in the lower slot for 2 seconds, then restore the carousel. If a wipe had
  started, reverse it back first. Nothing is sent. (Owner-decided)
- **EXP-039:** If the microphone fails after wipe-complete, treat it as
  finish: prepare and send the audio and sketch captured so far.
  (Owner-decided)
- **EXP-040:** After acceptance, publish only to the frozen recipients.
  The sender's inbox MUST NOT receive a card of this send. Return still
  restores the previous inbox selection and MUST NOT select a just-sent
  identity. (Owner-decided)
- **EXP-041:** Timeouts, connection reset, HTTP 5xx, and an expired or
  forbidden session keep this send queued (`no Wi-Fi` or `connecting`) and
  MUST NOT show `sent`. Unsupported or too-large media, and the same
  `client_message_id` with different content, are definite refusals
  (`couldn't send`, discard, no auto-retry). A malformed or unknown-schema
  success body is a wait, never `sent`. Frozen-recipient hangout refusal
  remains EXP-034. (Owner-decided; HTTP status numbers remain in the
  delivery contract)
- **EXP-042:** If frozen recipient names do not fit in the sending
  subtitle, show the first recipient's first name followed by `+N` for the
  remaining recipients. (Owner-decided)
- **EXP-043:** Sending status uses centered text without status icons;
  only `preparing` and `sending` use the already-defined progressive dots.
  (`sent` and `couldn't send` have no dots.) (Owner-decided)

### Traceability

| Source / decision | Requirements | Stages | Scenarios |
|---|---|---|---|
| SRC-OWNER-002 / DEC-001, DEC-002 | EXP-001, EXP-002 | Inbox exit, recipient selection | SCN-001 |
| SRC-OWNER-003 / DEC-003, DEC-013, DEC-014, DEC-015 | EXP-003–EXP-010 | Recipient selection, empty recipients | SCN-002–SCN-009 |
| SRC-OWNER-002 / DEC-005, DEC-006, DEC-011 | EXP-011 | Recording enter | SCN-006 |
| SRC-OWNER-004 / DEC-016, DEC-017 | EXP-016, EXP-017 | Recording enter, recording | SCN-010, SCN-011 |
| SRC-OWNER-002 / DEC-007, DEC-008, DEC-009 | EXP-012–EXP-014 | Recording finish, sending | SCN-011, SCN-012 |
| SRC-OWNER-004 / DEC-018, DEC-019 | EXP-018, EXP-019 | Preparing/sending/sent, inbox return | SCN-012, SCN-013 |
| SRC-OWNER-005 / DEC-020, DEC-021, DEC-022 | EXP-020–EXP-022 | Recording, preparing/sending/sent | SCN-012, SCN-014–SCN-017 |
| SRC-OWNER-006 / DEC-023, DEC-024, DEC-025 | EXP-023–EXP-025 | Wipe, preparing/sending/sent | SCN-018–SCN-021 |
| SRC-OWNER-007 / DEC-026, DEC-027, DEC-028 | EXP-026–EXP-028 | Recipient selection, recording, sending wait | SCN-022–SCN-025 |
| SRC-OWNER-008 / DEC-029, DEC-030, DEC-031 | EXP-029–EXP-031 | Sending wait, inbox re-entry | SCN-025–SCN-029 |
| SRC-OWNER-009 / DEC-032, DEC-033, DEC-034 | EXP-032–EXP-034 | Recording, queued send, refusal | SCN-030–SCN-033 |
| SRC-OWNER-010 / DEC-035, DEC-036, DEC-037 | EXP-035–EXP-037 | Power-loss, recipient selection, recording | SCN-034–SCN-039 |
| SRC-OWNER-011 / DEC-038, DEC-039 | EXP-038, EXP-039 | Recipient selection, recording | SCN-040–SCN-042 |
| SRC-OWNER-013 / DEC-041, DEC-042 | EXP-040, EXP-041 | Preparing/sending/sent, inbox return | SCN-043–SCN-047 |
| SRC-OWNER-014 / DEC-043–DEC-045 | EXP-006, EXP-007, EXP-010 | Recipient selection, empty recipients | SCN-048–SCN-050 |
| SRC-OWNER-015 / DEC-046, DEC-047 | EXP-018, EXP-020, EXP-042, EXP-043 | Preparing/sending/sent | SCN-051, SCN-052 |

## 9. Decisions, OPEN items, and readiness

### Intent ledger

| ID | Decision | Intended outcome / rationale | Source and date | Confidence | Status | Requirements |
|---|---|---|---|---|---|---|
| DEC-001 | This experience is the message-sending sequence that the inbox already exits into. Return lands on that same signed-in inbox | One signed-in send journey; inbox browsing stays in `EXP-MESSAGE-CAROUSEL`; re-entry staging is this spec's | SRC-OWNER-002, 2026-10-21 | High | OWNER-DECIDED | EXP-001 |
| DEC-002 | From the inbox, the red recording button pauses any active playback, keeps that message's audio position and sketch progress, stays signed in, and enters recipient selection | Matches already-decided inbox exit | SRC-EXP-CAR-001 `DEC-039`, 2026-10-21 | High | OWNER-DECIDED in `EXP-MESSAGE-CAROUSEL`; adopted here | EXP-002 |
| DEC-003 | Recipient selection uses the existing carousel in the established upper-third / lower-two-thirds split | Reuse the inbox/login selection language | SRC-OWNER-001 / SRC-OWNER-002, 2026-10-21 | High | OWNER-DECIDED | EXP-003 |
| DEC-004 | The person may select one or more recipients | Multi-recipient send is in scope; mechanism in DEC-013 | SRC-OWNER-001 / SRC-OWNER-003, 2026-10-21 | High | OWNER-DECIDED | EXP-005, EXP-008 |
| DEC-005 | When recipient selection is valid, the red button begins recording/sketching. Boot from this stage returns to the inbox and does not create a message | Contextual physical controls; cancel before capture | SRC-OWNER-001 / SRC-OWNER-002, 2026-10-21 | High | OWNER-DECIDED | EXP-008, EXP-009, EXP-011 |
| DEC-006 | Recording and sketching happen together on a full-screen surface. Sketch geometry is inherited from `EXP-SKETCH-RECORD-PLAYBACK`, not redesigned here | Same drawing the recipient will later see in a card | SRC-OWNER-002 / SRC-EXP-SKETCH-001, 2026-10-21 | High | OWNER-DECIDED | EXP-011 |
| DEC-007 | During recording, the red button finishes capture and begins preparing. Boot cancels, discards the unfinished message, and returns to the inbox | Cancel must not send a partial message | SRC-OWNER-001 / SRC-OWNER-002, 2026-10-21 | High | OWNER-DECIDED | EXP-012 |
| DEC-008 | After finish, preparing, sending, and sent confirmation are one continuous sequence, then the inbox returns automatically | Not separately navigable screens | SRC-OWNER-001 / SRC-OWNER-002, 2026-10-21 | High | OWNER-DECIDED | EXP-013 |
| DEC-009 | `Sent` means the server accepted the message for the selected recipients. It does not mean notification, playback, or delivery acknowledgment | Truthful success | SRC-OWNER-001 / SRC-OWNER-002 / SRC-COM-SEND-001, 2026-10-21 | High | OWNER-DECIDED | EXP-014 |
| DEC-010 | Split-panel connectivity reuses `connecting` (three progressive dots) and `no Wi-Fi`. Recording is not assumed to stop merely because the network is unavailable | Reuse established language; keep capture possible offline | SRC-OWNER-001 / SRC-OWNER-002 / SRC-OWNER-007 / SRC-EXP-LOGIN-001, 2026-10-21 | High | OWNER-DECIDED; applied in DEC-026–DEC-028 | EXP-026–EXP-028 |
| DEC-011 | Use the `lab.lab10` physical-button ring-wipe to enter recording, and the reverse red-circle collapse when finishing. Do not use the lab FAB bloom. Exact origin, radii, duration, easing, and clip match that lab. Collapse returns to the split pane (`DEC-018`) | Owner named this lab as the product motion | SRC-OWNER-002 / SRC-OWNER-004 / SRC-LAB-10-001, 2026-10-21 | High | OWNER-DECIDED; pixel parameters delegated | EXP-011, EXP-012, EXP-018 |
| DEC-012 | Mute continues to sign the user out and blank the display | Inherited login invariant | SRC-EXP-LOGIN-001 / SRC-OWNER-002, 2026-10-21 | High | OWNER-DECIDED in `EXP-LOGIN`; work fate in DEC-032–DEC-033 | EXP-032, EXP-033 |
| DEC-013 | Tap a person card to toggle them in or out of the recipient set. Swipe browses without changing selection. No Everyone card. No long-press. Nobody is pre-selected. Red starts recording only when at least one person is selected; otherwise it is ignored | Discoverable multi-select; no accidental send to an implied group | SRC-OWNER-003, 2026-10-21 | High | OWNER-DECIDED | EXP-005, EXP-008 | |
| DEC-014 | Centered/focused card is distinct from selection. Selected cards keep persistent selected chrome. Header is `Send a message to ...` when none are selected, `To [name]` for one, and `To` plus selected names for more than one | Person can see who will actually receive the message | SRC-OWNER-003, 2026-10-21 | High | OWNER-DECIDED | EXP-006, EXP-007 | |
| DEC-015 | Recipients are other hangout members, not the signed-in user. If nobody else is available, show `No people to message` in the lower slot, ignore red, and still allow Boot to return to the inbox | Never start a recording with no recipient or to self | SRC-OWNER-003, 2026-10-21 | High | OWNER-DECIDED | EXP-004, EXP-010 |
| DEC-016 | The sent message begins when the ring-wipe completes. Canvas and color rail are not interactable until then. Speech during the wipe is not part of the sent message | User can sketch only when the record surface is ready; that is the real start | SRC-OWNER-004, 2026-10-21 | High | OWNER-DECIDED | EXP-016 |
| DEC-017 | If the device can run the wipe and capture together, start hardware capture during the wipe so leading abrupt sound can be clipped. Clip kept audio to wipe-complete. If overlap is not affordable, start at wipe-complete and still clip hardware onset. Do not copy a legacy clip duration | Hardware startup noise is not the start of the message | SRC-OWNER-004, 2026-10-21 | High | OWNER-DECIDED; clip duration delegated | EXP-017 |
| DEC-018 | Reverse collapse returns to the split pane. Header is `PANEL-HEADER-TITLE-SUBTITLE`, stable through preparing, sending, and confirmation: title `sending to ...`, subtitle the frozen recipient names. Lower body replaces the carousel with centered status copy (`DEC-020`) | Header keeps who the message is for; body carries phase | SRC-OWNER-004 / SRC-OWNER-005, 2026-10-21 | High | OWNER-DECIDED | EXP-018, EXP-020 |
| DEC-019 | Keep sent confirmation visible for 2 seconds, then return to the inbox on the previously selected message, still paused, with no autoplay. Do not select the just-sent message | Person reads confirmation and lands where they left | SRC-OWNER-004, 2026-10-21 | High | OWNER-DECIDED | EXP-019 |
| DEC-020 | Lower body uses the centered status presentation: `preparing` with three progressive dots, then `sending` with three progressive dots, then `sent` with no dots. No overlay and no dim | Phase is visible without restating recipients | SRC-OWNER-005, 2026-10-21 | High | OWNER-DECIDED | EXP-020 |
| DEC-021 | No on-screen recording timer or recording indicator. The live canvas is the cue that recording has started. Audio-only (no sketch) remains valid | Avoid new chrome on the inherited record layout | SRC-OWNER-005, 2026-10-21 | High | OWNER-DECIDED | EXP-021 |
| DEC-022 | Auto-finish at 3 minutes from wipe-complete, same path as red. Do not auto-stop on silence | Cap stuck recordings; sketch-led messages must not be discarded | SRC-OWNER-005, 2026-10-21 | High | OWNER-DECIDED | EXP-022 |
| DEC-023 | During the ring-wipe, Boot cancels, discards, and returns to the inbox. Red is ignored | Cancel before a usable canvas; do not finish an empty recording | SRC-OWNER-006, 2026-10-21 | High | OWNER-DECIDED | EXP-023 |
| DEC-024 | During `preparing`, `sending`, and `sent`, red is ignored. Boot is ignored except the connectivity-wait exception in DEC-031 | Finish was the last cancel of the message itself; leaving a wait is not cancel | SRC-OWNER-006 / SRC-OWNER-008, 2026-10-21 | High | OWNER-DECIDED; Boot wait-exit in DEC-031 | EXP-024, EXP-031 |
| DEC-025 | During `sent` confirmation, red and Boot are ignored; wait out the 2 seconds | Confirmation is already a timed return | SRC-OWNER-006, 2026-10-21 | High | OWNER-DECIDED | EXP-025 |
| DEC-026 | Keep a known recipient list and selection through connectivity loss. Red may start recording offline. Show `connecting` / `no Wi-Fi` only when no member list is known. Assumes local offline capture is possible; if engineering finds it is not, return to the owner | Do not block a valid send when people are already chosen | SRC-OWNER-007, 2026-10-21 | High | OWNER-DECIDED; feasibility assumed | EXP-026 |
| DEC-027 | Connectivity loss during recording does not stop capture and does not cover the canvas with connectivity copy | Capture is local | SRC-OWNER-007, 2026-10-21 | High | OWNER-DECIDED | EXP-027 |
| DEC-028 | After finish, if the server has not accepted this send, keep `sending to ...`, show `no Wi-Fi` or `connecting` in the body, and do not show `sent`. Same send operation | Truthful wait; no false `sent` | SRC-OWNER-007, 2026-10-21 | High | OWNER-DECIDED | EXP-028 |
| DEC-029 | Keep the finished audio, optional sketch, and frozen recipients until this same send is accepted. Do not discard for connectivity loss. Do not create a second message | She already finished; retry is the same operation | SRC-OWNER-008, 2026-10-21 | High | OWNER-DECIDED | EXP-029 |
| DEC-030 | When Wi-Fi is up and the server can accept, submit this send automatically without another red press | Finish already meant send | SRC-OWNER-008, 2026-10-21 | High | OWNER-DECIDED; retry cadence delegated | EXP-030 |
| DEC-031 | Boot from `no Wi-Fi` / `connecting` after finish returns to the inbox without discarding. Skip `sent` if she already left. Red from the inbox while pending returns to this wait screen and does not start another recording | Leaving a wait is not cancel; one send at a time | SRC-OWNER-008, 2026-10-21 | High | OWNER-DECIDED | EXP-031 |
| DEC-032 | Mute during recording or the ring-wipe discards unfinished capture and signs out | Unfinished work is not a queued send | SRC-OWNER-009, 2026-10-21 | High | OWNER-DECIDED | EXP-032 |
| DEC-033 | Mute after finish keeps the queued send, does not upload while signed out, and resumes that same send when that user signs in again. Login still enters the inbox. Skip `sent` if accepted while away. Do not run it as a different user | Finish meant send; mute is session off, not cancel | SRC-OWNER-009, 2026-10-21 | High | OWNER-DECIDED | EXP-033 |
| DEC-034 | Definite server refusal is atomic: nobody receives it. Show `couldn't send` for 2 seconds, then inbox, and discard that attempt. Do not auto-retry a refusal. Do not show `sent` | Refusal is not a connectivity wait; no silent partial delivery | SRC-OWNER-009, 2026-10-21 | High | OWNER-DECIDED | EXP-034 |
| DEC-035 | Power loss discards unfinished capture. A finished queued send survives restart if local storage does, then resumes when that user signs in. If storage is gone, there is no send and no recovery UI | Same as mute for unfinished work; finished work should not vanish unless the device lost it | SRC-OWNER-010, 2026-10-21 | High | OWNER-DECIDED | EXP-035 |
| DEC-036 | Before freeze, a person who leaves the hangout is removed from the carousel and selection. Empty selection ignores red. Remaining selections stay. New members appear unselected | Do not start recording for someone who is no longer a recipient | SRC-OWNER-010, 2026-10-21 | High | OWNER-DECIDED | EXP-036 |
| DEC-037 | When the sketch buffer is full, ignore further drawing, keep audio, and do not auto-finish | A full sketch must not kill the spoken message | SRC-OWNER-010, 2026-10-21 | High | OWNER-DECIDED; cap size delegated | EXP-037 |
| DEC-038 | If capture cannot start, keep recipient selection and the current selection. Show `couldn't record` for 2 seconds. Reverse a started wipe back first. Nothing is sent | Every message needs audio; do not lose the chosen people | SRC-OWNER-011, 2026-10-21 | High | OWNER-DECIDED | EXP-038 |
| DEC-039 | Microphone failure after wipe-complete is a finish, not a cancel. Prepare and send what was captured | She already spoke | SRC-OWNER-011, 2026-10-21 | High | OWNER-DECIDED | EXP-039 |
| DEC-040 | Exception checkpoint: ring-wipe pixels, leading-audio clip length, sketch buffer size, retry cadence, storage medium, and interrupted-wipe motion are delegated under their existing invariants | Remaining uncertainty is engineering, not product policy | SRC-OWNER-011, 2026-10-21 | High | OWNER-DECIDED | — |
| DEC-041 | After acceptance, publish only to the frozen recipients. The sender's inbox does not receive a copy. Return still restores the previous selection and does not select a just-sent identity | Self is not a recipient; inbox is incoming mail | SRC-OWNER-013, 2026-10-21 | High | OWNER-DECIDED | EXP-040 |
| DEC-042 | Timeouts, 5xx, connection reset, and expired/forbidden session keep waiting. Unsupported or too-large media and `client_message_id` content conflict are `couldn't send`. Malformed success bodies wait and never show `sent` | Copy is already decided; this is which results use it | SRC-OWNER-013, 2026-10-21 | High | OWNER-DECIDED; HTTP numbers delegated | EXP-041 |
| DEC-043 | When selected names exceed the 25-character `To` header limit, show the first selected recipient's first name followed by `+N` for the remaining recipients | Preserve recipient identity and make multi-selection visible in a short header | SRC-OWNER-014, 2026-10-21 | High | OWNER-DECIDED | EXP-007 |
| DEC-044 | A selected recipient card uses the 2 px accent outline as its only selection cue; no checkmark | Keep selection visible without adding a second cue | SRC-OWNER-014, 2026-10-21 | High | OWNER-DECIDED | EXP-006 |
| DEC-045 | With no other hangout members, retain the split pane and show centered `No people to message` in the lower slot, without cards or an illustration | Keep the established empty-state copy simple and in place | SRC-OWNER-014, 2026-10-21 | High | OWNER-DECIDED | EXP-010 |
| DEC-046 | If frozen recipient names do not fit in the sending subtitle, show the first recipient's first name followed by `+N` for the remaining recipients | Keep the recipient summary compact while retaining the first name and multi-recipient count | SRC-OWNER-015, 2026-10-21 | High | OWNER-DECIDED | EXP-018, EXP-042 |
| DEC-047 | Sending status remains centered text without status icons; progressive dots appear only for `preparing` and `sending` | Preserve the simple status treatment across progress and result | SRC-OWNER-015, 2026-10-21 | High | OWNER-DECIDED | EXP-020, EXP-043 |

### Locked behavior

- This is the inbox's send journey and returns to that same signed-in inbox
  (`DEC-001`).
- Inbox red-button entry pauses playback and keeps position/sketch (`DEC-002`).
- Recipient selection uses the split-panel carousel (`DEC-003`).
- Tap-to-toggle one or more other hangout members; no Everyone; no
  long-press; none pre-selected; red ignored until at least one is selected
  (`DEC-004`, `DEC-013`, `DEC-015`).
- Focus is not selection. Selected cards use only the 2 px accent outline;
  long recipient headers use first name + `+N` (`DEC-014`, `DEC-043`,
  `DEC-044`).
- Empty available recipients keep the split pane and show centered
  `No people to message`, with no cards or illustration (`DEC-015`,
  `DEC-045`).
- Valid selection + red begins recording; Boot from selection returns with
  no message (`DEC-005`).
- Recording and sketching share the inherited full-screen canvas (`DEC-006`).
- Red finishes capture; Boot during recording discards and returns (`DEC-007`).
- Preparing, sending, and sent are one continuous sequence, then auto-return
  (`DEC-008`).
- `Sent` means server acceptance only (`DEC-009`).
- Split-panel connectivity copy is inherited; recording is not assumed to
  stop offline (`DEC-010`).
- Recording enter/finish uses the `lab.lab10` physical ring-wipe and reverse
  collapse (`DEC-011`).
- Mute still signs out (`DEC-012`). Unfinished capture is discarded
  (`DEC-032`). A finished send is kept and resumes after that user signs
  in (`DEC-033`).
- Sent content and sketching start when the wipe completes; wipe-period
  speech is discarded (`DEC-016`). Hardware may capture earlier only to
  clip startup noise (`DEC-017`).
- Reverse collapse returns to split pane. Sending header is
  `sending to ...` plus the frozen names on `PANEL-HEADER-TITLE-SUBTITLE`;
  overflow uses first name + `+N` (`DEC-018`, `DEC-046`).
- Sent confirmation lasts 2 seconds, then the previous inbox selection
  returns paused (`DEC-019`).
- Lower body uses centered text without icons; only `preparing` and
  `sending` show progressive dots (`DEC-020`, `DEC-047`).
- No record timer or indicator (`DEC-021`).
- 3-minute auto-finish; no silence auto-stop (`DEC-022`).
- Wipe: Boot cancels; red ignored (`DEC-023`).
- Preparing, `sending`, and `sent`: red ignored; Boot ignored except
  connectivity wait (`DEC-024`, `DEC-025`, `DEC-031`).
- Known recipient list survives an outage; recording may start offline
  (`DEC-026`).
- Recording continues through connectivity loss (`DEC-027`).
- Sending wait shows `connecting` / `no Wi-Fi`, never `sent` (`DEC-028`).
- Finished work is kept until this same send is accepted and is submitted
  automatically (`DEC-029`, `DEC-030`).
- Boot from connectivity wait leaves without discarding; one pending send
  at a time (`DEC-031`).
- Definite refusal shows `couldn't send`, is all-or-nothing, then discards
  that attempt (`DEC-034`).
- Power loss drops unfinished capture and keeps a finished send only if
  storage survives (`DEC-035`).
- Hangout members who leave before freeze are dropped from selection
  (`DEC-036`).
- A full sketch ignores further ink and keeps audio (`DEC-037`).
- Capture that cannot start shows `couldn't record` and keeps selection
  (`DEC-038`). Microphone failure after recording starts finishes and
  sends (`DEC-039`).
- After acceptance, only frozen recipients receive the message. The
  sender's inbox does not gain a card of this send (`DEC-041`).
- Timeouts, 5xx, connection reset, and expired/forbidden session keep
  waiting. Unsupported or too-large media and a `client_message_id`
  content conflict are `couldn't send` (`DEC-042`).

### Bounded variation

None yet.

### Delegated engineering choices

- `lab.lab10` ring-wipe origin, inner/outer radii, duration, easing, and
  clip geometry, provided the visible motion matches that lab's physical
  ring-wipe rather than its FAB bloom (`DEC-011`).
- Order of recipient cards among other hangout members, provided the
  signed-in user is omitted and membership stays the current hangout
  (`DEC-015`).
- Leading-audio clip duration, provided kept audio and sketch start at
  wipe-complete (`DEC-017`).
- Interrupted-wipe motion, provided Boot during wipe discards and returns
  to the inbox with no send (`DEC-023`).
- Whether offline capture uses RAM, flash, or another medium, provided the
  recording can continue and a finished message can be retained until the
  owner-decided send policy runs (`DEC-026`). If that is not feasible,
  stop and return to the owner.

- Retry cadence and backoff while waiting for acceptance, provided this
  remains one send and `sent` is shown only after acceptance (`DEC-030`).
- Upload chunking and local storage medium, provided finished work survives
  until acceptance or an owner-decided discard (`DEC-029`, `DEC-035`).
- Sketch buffer capacity, provided further ink is ignored and audio
  continues when it is full (`DEC-037`).
- Exact HTTP status numbers for the refusal-versus-wait classes, provided
  the user-visible map in `DEC-042` is preserved.

### OPEN items

| ID | Topic | Consequence | Blocks implementation? |
|---|---|---|---|
| OPEN-001 | *(resolved)* `lab.lab10` motion adopted as `DEC-011` | — | No |
| OPEN-002 | *(resolved)* Tap-to-toggle selection adopted as `DEC-013` / `DEC-014` | — | No |
| OPEN-003 | *(resolved)* Hangout-member removal and append during selection adopted as `DEC-036` | — | No |
| OPEN-004 | *(resolved)* No timer/indicator, 3-minute auto-finish, no silence stop, and full-sketch ignore-ink adopted as `DEC-021` / `DEC-022` / `DEC-037` | — | No |
| OPEN-005 | *(resolved)* Red/Boot during wipe, preparing, sending, and `sent` adopted as `DEC-023`–`DEC-025` | — | No |
| OPEN-006 | *(resolved)* Retention, auto-send, leave-without-discard, mute, and definite refusal adopted as `DEC-029`–`DEC-034` | — | No |
| OPEN-007 | *(resolved)* Recipient selection, recording, and sending-wait connectivity adopted as `DEC-026`–`DEC-028` | — | No |
| OPEN-008 | *(resolved)* 2-second confirmation and previous-selection return adopted as `DEC-019` | — | No |
| OPEN-009 | *(resolved)* Power-loss adopted as `DEC-035` | — | No |
| OPEN-010 | *(resolved)* Capture-start and mid-recording mic failure adopted as `DEC-038` / `DEC-039` | — | No |
| OPEN-011 | *(resolved)* Recipients-only publication adopted as `DEC-041` | — | No |
| OPEN-012 | *(resolved)* Refusal-versus-wait classes adopted as `DEC-042` | — | No |

### Challenge walkthroughs

- **Ordinary:** Maya pauses Jordan's message, selects Sam, records and
  draws, finishes with red, sees `preparing` / `sending` / `sent`, and
  returns to Jordan paused. (SCN-001, SCN-003, SCN-011–SCN-013)
- **Interrupted:** She finishes offline, Boots from `no Wi-Fi` to the
  inbox; the same send continues and does not show `sent` if accepted
  after she left. (SCN-027, SCN-029)
- **Failure:** Microphone unavailable keeps selection and shows
  `couldn't record`. Unsupported media or a content-conflict is
  `couldn't send`. Timeouts and 5xx keep waiting. (SCN-040, SCN-033,
  SCN-044–SCN-047)
- **Boundary:** 3-minute auto-finish; full sketch ignores ink; power loss
  drops unfinished work and keeps a finished send only if storage
  survives. (SCN-016, SCN-039, SCN-034–SCN-036)

### Review criteria

| Criterion | Rating | Notes |
|---|---|---|
| Concrete | CLEAR | Desk endpoint, red/Boot/mute, named copy, Maya/Jordan/Sam examples |
| Understandable | CLEAR | Each stage lists available actions and what follows |
| Complete | CLEAR | Ordinary, cancel, connectivity, mute, refusal, power-loss, capture failure, drawing cap, sender-copy, and refusal-vs-wait classes are decided or delegated |
| Truthful | CLEAR | `sent` is server acceptance only; wait copy is not success |
| Continuous | CLEAR | Inbox play heads survive; queued send is one operation; late `sent` does not steal the inbox |
| Consistent | CLEAR | Copy, controls, and scenarios agree after DEC-024's wait-exit exception |
| Implementable | CLEAR | Product policy does not depend on an OPEN item; wire schema stays a separate contract |
| Testable | CLEAR | Scenarios name observable copy, controls, and retained work |
| Traceable | CLEAR | DEC → EXP → SCN map is bidirectional for issued IDs |
| Proportionate | CLEAR | Motion pixels, clip length, buffer size, and retry cadence are delegated |

### Contract checkpoint (confirmed 2026-10-21)

The owner reviewed the locked ordinary journey, cancel rules, truthful
`sent`, keep/resume policy, and delegated engineering set (`DEC-040`) and
confirmed them (`SRC-OWNER-012`).

Locked behavior is the list above. Bounded variation: none; the 2-second
`sent`, `couldn't send`, and `couldn't record` dwells are exact.

Delegated choices are the list above.

No blocking OPEN product items remain in this experience. Sender-copy and
refusal-versus-wait classes were accepted after the original contract
checkpoint (`SRC-OWNER-013`, `DEC-041`, `DEC-042`). Offline capture
feasibility is an engineering assumption: if it is not possible, return to
the owner. Message-delivery routes, codecs, chunking, retention, and HTTP
status numbers remain in the communication contract and do not change this
user-visible policy.

This confirmation records what was reviewed. It is not product-wide
approval and does not authorize implementation.

### Readiness

| Gate | Status |
|---|---|
| Reconstruction checkpoint | Confirmed 2026-10-21 |
| Ordinary-flow checkpoint | Confirmed 2026-10-21 |
| Exception checkpoint | Confirmed 2026-10-21 |
| Contract checkpoint | Confirmed 2026-10-21 |
| Analysis-ready | Yes |
| Implementation-ready | Yes for this experience's user-visible behavior. Does not authorize firmware, server, or UI implementation. Wire schema is a separate communication contract. Offline storage feasibility must be verified; if it fails, return to the owner |
