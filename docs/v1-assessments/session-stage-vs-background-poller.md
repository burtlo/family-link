# Concept guide: session stage machine versus background poller

Status: explanatory guidance with v1 device examples  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md) §§3 and 6  
Related assessment: [authentication/connectivity](authentication-connectivity.md)

## Definition

A session stage machine describes what the user is doing and what actions are
currently allowed. A background poller periodically gathers external facts. They
are different structures with different authority.

The stage machine may say:

```text
connecting | roster | pin_entry | pin_verifying | carousel
picker | recording | sending | settings | asleep
```

The poller may report:

```text
link state
server readiness
new inbox generation
cached-data freshness
device/peripheral health
```

The poller does not own stages. It produces evidence. The session owner combines
that evidence with the active stage and operation to decide whether to ignore,
defer, merge, cancel, or transition.

This separation prevents a low-priority heartbeat from erasing a high-priority
user action.

## Three distinct models

### Session stage

Answers:

- What task is the user performing?
- Which inputs are accepted?
- Which scene should be shown?
- Which operations may be submitted?

Exactly one reducer owns transitions.

### Dependency truth

Answers:

- Is the physical/network link available?
- Has the application server recently answered?
- Is cached data present, and how fresh is it?

Truth may be uncertain or stale. It is not automatically a stage.

### Poll schedule

Answers:

- When should another observation be attempted?
- What is the timeout, interval/backoff, and jitter?
- Is polling suspended by an active user operation?

The schedule is policy, not presentation.

## Required poll-result policy

For every stage and poller pair, define one action:

- **ignore:** result is irrelevant in this stage;
- **defer:** retain or re-request after the user operation terminates;
- **merge:** update dependency truth without changing stage;
- **cancel:** ask the session owner to cancel an incompatible operation;
- **transition:** permitted only when the operational contract explicitly names
  the transition.

The result must carry an operation ID and observation time so a late response can
be recognized as stale.

## Reducer shape

```text
reduce(session, event):
  ProbeSucceeded(snapshot, observed_at, probe_id)
  ProbeFailed(reason, observed_at, probe_id)
  LoginSubmitted(login_id)
  LoginCompleted(login_id, outcome)
  InboxChanged(generation)
  LinkChanged(state)
```

The reducer checks current stage and operation identity. Presentation renders the
resulting snapshot. The poller never calls `paint`, changes a screen enum, or
clears user input.

## V1 example 1: hangout probe versus PIN verification

### Current employment

V1 recognizes the conflict. `v1_state_may_probe()` refuses a probe while
`v1_auth_login_active()`, and `v1_connect_enter_from_signin()` will not enter
connecting during active login.

These gates were added to prevent a signed-out probe from clobbering
`checking...`.

### Structural limitation

The policy exists as two Boolean checks rather than a stage/poller decision. It
does not define what happens to a probe already in flight, whether its result is
deferred, or how partial PIN input is reconciled.

### Concrete v1 policy

```text
stage = pin_verifying, event = ProbeFailed
  -> merge dependency truth as unavailable
  -> do not clear PIN or change stage
  -> let LoginOutcome decide wrong-pin versus connecting

stage = pin_entry, no operation, event = ProbeFailed
  -> transition to connecting
  -> clear or preserve partial PIN according to contract
```

The two rows are intentionally different because an active user operation has
higher authority than a background observation.

## V1 example 2: connecting retry versus dot animation

### Current employment

V1 separates dot mutation into `v1_connect_refresh_conn_dots()`, but
`v1_connect_tick()` may synchronously execute the probe on the same `ui_task`.
The animation therefore cannot run while the poll attempt waits.

### Concrete v1 separation

- Poll scheduler creates `ProbeOperation` every retry interval.
- I/O worker performs the attempt with its deadline.
- Presentation animation advances from monotonic time independently.
- `ProbeSucceeded` may transition connecting to roster.
- `ProbeFailed` updates attempt/freshness and leaves stage connecting.

The visual retry cadence is no longer coupled to request duration.

## V1 example 3: inbox WebSocket notification versus record/send

### Current employment

`v1_carousel_tick_inbox()` detects `ST_RECORD` or `ST_SEND`, restores
`s_inbox_dirty`, and defers reload. This is a useful instance of poll/event
priority: inbox refresh should not disrupt user-created work.

### Concrete improvement

Use an inbox generation rather than a Boolean dirty flag:

```text
InboxInvalidated(generation)

stage = recording or sending
  -> remember highest pending generation

on terminal SendOutcome
  -> launch one inbox refresh for highest generation
```

This coalesces repeated notifications and makes queue loss or stale reload visible.

## V1 example 4: signed-in offline versus connecting

### Current employment

The product distinguishes:

- signed out + server unavailable: connecting stage;
- signed in + server unavailable: remain in carousel with offline ribbon.

That is exactly the distinction between dependency truth and session stage.

### Concrete policy

```text
stage = carousel, event = ServerUnavailable
  -> merge server truth
  -> remain carousel
  -> disable/block server-backed actions
  -> preserve local focus and readable cached content

stage = roster, event = ServerUnavailable
  -> transition connecting
```

Whether playback remains available depends on the separate offline-media contract;
the poller must not imply availability.

## V1 example 5: Wi-Fi retry versus active media work

### Current behavior

Wi-Fi rejoin happens from `ui_task` only in `ST_WIFI_ERR`, but the broader
architecture does not define priority between link recovery and capture/playback.
Future stage additions could accidentally run link recovery in a media-sensitive
stage.

### Concrete policy

- Link observer may report down in every stage.
- Automatic join is an I/O operation owned by connectivity infrastructure.
- The session reducer decides whether active playback continues, recording is
  cancelled, or upload moves to retained/pending work.
- Link recovery never blocks presentation or directly paints Wi-Fi UI.

## Designing a stage/poller matrix

For each user stage, document:

- permitted pollers;
- result action: ignore, defer, merge, cancel, transition;
- user input/work preserved;
- operation with priority over poller;
- maximum truth staleness;
- resulting copy or badge.

Do not use a single global rule such as “offline always means connecting.” V1
already demonstrates why signed-in and signed-out behavior differ.

## Tests that prove separation

- Complete an old probe after a newer probe; old truth is rejected as stale.
- Fail a probe during login; the configured policy is followed without indefinite
  `checking...`.
- Deliver many inbox notifications during recording; one refresh occurs after
  terminal outcome.
- Link loss while signed in changes availability, not user stage.
- Probe timeout cannot pause connecting animation.
- Cached data never causes a readiness transition without required server proof.

## Review checklist

- Is this value a user stage, dependency truth, or schedule?
- Who has authority to transition the stage?
- What does each poll result do during every active user operation?
- Can an old result overwrite newer truth?
- Does polling run independently of presentation?
- Are cached data and application readiness clearly distinct?
