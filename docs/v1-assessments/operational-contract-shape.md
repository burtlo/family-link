# Concept guide: operational contract document

Status: explanatory guidance with v1 device examples  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md) §7  
Related plan: `docs/plans/operational-contract.md`

## Definition

An operational contract defines what the user experiences while software waits,
retries, degrades, succeeds, fails, cancels, or times out. It sits between product
intent and implementation.

It is user- and behavior-oriented, not library- or hardware-oriented. It should
remain useful if the client moves from LVGL to another toolkit, from FreeRTOS to
another scheduler, or from one device generation to another.

An operational contract does not prescribe task names, callback APIs, widget
types, or transport libraries. It defines observable stages and invariants that
all implementations and surfaces must honor.

## Why a separate contract is needed

Architecture says where code belongs. Coding standards say how it must be
structured. The operational contract says what must happen.

Without it:

- a timeout constant may exist without any behavior using it;
- transport failure may be presented as a credential error;
- firmware and a browser twin may look similar but retry differently;
- a developer may invent an outbox, retry, or error message while fixing a bug;
- tests cannot distinguish a product decision from current implementation.

## Document shape

Begin with:

1. product surfaces covered;
2. authoritative source documents;
3. terminology for stages, operations, and dependency truth;
4. shared failure-message rules;
5. shared retry/timing policy;
6. one entry for every user-visible wait or long-running action;
7. explicit OPEN decisions.

## Stage entry template

Use this shape for each stage:

```text
Stage name
  Purpose
  Entry trigger
  Inputs/work preserved
  Immediate visible response
  Permitted actions
  Background activity allowed

  Success
    condition
    next stage
    visible response

  Failure class
    detection rule
    retry policy
    next stage
    exact copy and surface
    retained/discarded work

  Cancellation
    trigger
    next stage
    retained/discarded work

  Timeout
    attempt timeout
    total deadline
    terminal behavior

  Never acceptable
  Verification scenarios
  Surface differences
  OPEN decisions
```

Use separate failure classes when the user response differs. “Request failed” is
not sufficient when wrong credentials, server unavailability, and local resource
failure lead to different stages.

## Shared policy sections

### Failure messages

Define:

- calm waiting versus actionable error;
- transient toast versus persistent stage;
- exact copy where decided;
- forbidden technical details;
- which failures require escalation to a person;
- whether partial input or user-created work is retained.

### Retry and timeout

For every retrying action define:

- per-attempt timeout;
- interval, backoff, and jitter;
- maximum attempts or stopping condition;
- user-visible state while retrying;
- interaction with active user operations;
- one source for constants across surfaces.

### Degraded behavior

Define what remains usable:

- local/cached read;
- playback;
- capture;
- send;
- settings;
- cancellation and navigation.

“Offline” alone is not a complete contract.

## Decided versus OPEN

A contract must not manufacture completeness.

- **Decided:** supported by product specification or explicit maintainer decision.
- **OPEN:** sources conflict, user experience is unspecified, or current code is
  merely incidental.
- **As-built divergence:** implementation does not match a decided row.

OPEN behavior belongs in a clearly separated section. Implementers may not resolve
it silently.

## V1 example 1: connecting

### Contract shape

```text
Stage: connecting
Entry: signed out and hangout readiness probe fails
Immediate UX: mailbox, "connecting", progressive dots
Retry: every 5 s; 2.5 s attempt timeout
Success: 200 response with users -> roster
Failure: remain connecting and retry
Wi-Fi down: separate no-Wi-Fi stage
Never acceptable:
  cached roster shown as ready
  timeout to developer error
  frozen dot animation
```

### What the contract reveals

The behavior is well specified, but v1 runs the probe on `ui_task`, so the
animation can stall. The contract remains correct even though the implementation
violates presentation standards.

It also reveals private timing values—15-second boot load and 25-second Wi-Fi
join—that must either enter the contract or be removed.

## V1 example 2: PIN verification

### Contract shape

```text
Stage: PIN verification
Entry: fourth digit
Immediate UX: fourth dot and "checking..."
Success: authenticated -> carousel
Wrong credential: clear entry, "wrong pin"; apply lockout policy
Server/transport unavailable: connecting; do not count wrong PIN
Further taps: ignored while operation active
Never acceptable: indefinite "checking..."
```

### What the contract reveals

The Boolean return from `v1_api_login_user` cannot faithfully implement these
failure classes. A typed result is required. The contract also exposes the
2.5-second documented timeout versus 6-second as-built timeout.

## V1 example 3: recipient picker

### Contract shape

```text
Stage: recipient picker
Entry: send action from carousel
Success: recipients frozen -> recording
Cancel: shoulder -> carousel, no upload
Timeout: 10 s -> defined cancellation/return behavior
Offline: block send with decided copy
Never acceptable: stale selection or recording wrong recipient
```

### What the contract reveals

`V1_UI_PICK_TIMEOUT_MS` exists, but the product implementation returns false from
`v1_record_tick_pick_timeout`. This is a contract failure that constant-parity
checks cannot detect.

The exact timeout copy and whether selection persists are not fully specified and
should remain OPEN until decided.

## V1 example 4: send and upload

### Contract shape

```text
Stages: finishing -> sending -> sent | failed
Success: server acceptance -> "Sent" receipt -> carousel
Failure: "Couldn't send"; current v1 discards work
Receipt dwell: approximately 2 s
Total stuck guard: 25 s as built
Cancel: allowed during picker/record, not receipt
Never acceptable: false success, duplicate upload, indefinite sending
```

### What the contract reveals

Multi-recipient partial success is unspecified. The implementation can deliver to
some recipients and still show a generic failure. An outbox and retry behavior are
also OPEN. The contract should record those gaps rather than selecting a design.

## V1 example 5: playback while offline

### Contract shape

```text
Stage: signed-in carousel with server unavailable
Stage behavior: remain carousel, show offline ribbon
Send: blocked
Playback: OPEN/CONFLICT
Position/read sync: define fail-soft or retained policy
Never acceptable: connecting screen while signed in
```

### What the contract reveals

Documentation has implied that saved messages still play, while v1 fetches blobs
from the server and may show `can't play right now`. This is a product/implementation
conflict, not merely a code bug. The contract must choose truthful behavior before
the next client implements caching or copy.

## Contract tests

Each decided stage row should produce executable scenarios:

- given stage and dependency truth;
- when a user intent or operation outcome occurs;
- then next stage, visible copy, preserved work, and retry schedule match.

Run the same scenario vocabulary on device, twin, and any future client. Platform
adapters may differ; expected behavior does not.

## Review checklist

- Are all waits and long-running actions represented?
- Are failure classes based on user-visible differences?
- Are exact copy, retry, timeout, and stopping conditions explicit?
- Is preserved/discarded user work stated?
- Are background poller effects defined?
- Are never-acceptable outcomes testable?
- Are undecided behaviors marked OPEN rather than invented?
