# V1 assessment: authentication and connectivity

Status: as-built assessment, not a v2 design  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md)  
Primary code: `firmware/v1/v1_auth.c`, `v1_connect.c`, `v1_state.c`,
`v1_api.c`, `x02_main.c`  
Product contract: `docs/BOX-UI.md` § Connecting and § Connection confidence

## Purpose and boundary

This subsystem joins the network, determines whether the application server is
ready, paints connecting/Wi-Fi/roster/PIN stages, verifies credentials, enforces
PIN lockout, and routes successful login into the signed-in session.

The intended boundary is:

- presentation: roster, PIN, connecting animation, Wi-Fi failure screen;
- session: signed-out stage, selected identity, active login operation, lockout;
- I/O: network join, hangout probe, login request, cache persistence.

V1 partially separates login I/O into a worker, but probes and Wi-Fi recovery still
run from the presentation loop. Session state can be assigned by several feature
modules in addition to the event drain.

## User-visible stages

### Wi-Fi unavailable

- Entry: network join failure.
- UX: `no Wi-Fi`, home-network guidance, ask Lynn.
- Retry: v1 retries from `ui_task`.
- Success: connecting, then roster only after an application-level hangout response.
- Never acceptable: presenting cached roster as ready while the server is
  unreachable.

### Connecting

- Entry: signed out and `/v1/hangout` is unavailable.
- UX: `connecting` and progressive dot animation.
- Retry contract: 5-second interval, 2.5-second probe timeout, no timeout to a
  separate error stage.
- Success: a 200 response with users, then roster.
- Never acceptable: blocked animation, stale roster tiles, or developer copy.

### Roster and PIN entry

- Entry: application readiness established.
- UX: choose a user, enter four digits with immediate local dots.
- Background policy: signed-out reachability continues, but it must not clobber an
  active login.
- Never acceptable: clearing partial input because an unrelated poll completed.

### PIN verification

- Entry: fourth digit.
- Immediate UX: fourth dot and `checking...`.
- Success: signed-in carousel.
- Wrong credential: clear entry and show `wrong pin`; five failures produce a
  60-second `ask Lynn` cooldown.
- Transport/server failure: connecting, not wrong PIN.
- Never acceptable: indefinite `checking...`, missing fourth-dot feedback, or a
  stale worker result signing in the wrong user.

## What v1 gets right

- Login copies user, PIN, and generation before waking the worker.
- Login HTTP runs on a worker rather than the keypad callback.
- Further keypad taps are ignored while login is busy.
- Successful login is posted through the state event queue with a generation.
- Stale and offline worker paths at least request UI reconciliation instead of
  silently returning.
- Connecting requires an application-level hangout response, not merely a
  WebSocket or cached roster.
- The connecting screen is distinct from Wi-Fi failure and signed-in offline.
- Roster UI handles are explicitly invalidated before another scene destroys them.
- Shared timing exists for probe, retry, Wi-Fi retry, PIN attempts, and cooldown.

## Standards assessment

### Critical — probes and Wi-Fi recovery block presentation

Violates Standards §§1–2.

`x02_main.ui_task` calls `v1_connect_tick`, which may synchronously perform a
2.5-second hangout probe. The same loop performs `wifi_sta_join` with a 25-second
timeout and then a 15-second hangout load. During these waits, input, connecting
dots, repaints, auth ticks, and other presentation work cannot run.

This is the strongest source-level explanation for a connecting screen that
animates irregularly or appears locked.

Required next-version boundary:

- join and probe are operations on an I/O owner;
- the session owner receives `LinkChanged`, `ProbeSucceeded`, `ProbeFailed`, and
  `ProbeTimedOut`;
- animation is independent of retry execution;
- retry scheduling uses timers, not sleeps in presentation.

### Critical — session state does not have one writer

Violates Standards §3.

`v1_state_drain` is intended to own transitions, but feature code also calls
`v1_state_apply` directly. This permits worker and presentation paths to bypass
transition guards, generation checks, and centralized logging.

The state queue also has a fixed length of eight, ignores `xQueueSend` failure, and
therefore can silently discard a transition.

Required next-version rules:

- only the session reducer applies stage changes;
- every event submission reports accepted, coalesced, or rejected;
- queue capacity and overflow strategy are tested;
- all transitions emit before/after telemetry.

### High — login terminal outcomes are incomplete

Violates Standards §4.

V1 has a generation but not a complete operation envelope. `V1_EV_AUTH_FAIL` and
`V1_EV_AUTH_STALE` exist yet have no reducer behavior, while stale branches
directly clear `busy` and request a repaint. Cancellation and timeout are not
first-class outcomes.

The next client should use:

```text
LoginOperation(id, user_id, credential_snapshot)
  -> authenticated(session)
  -> rejected(wrong_credential)
  -> failed(transport_or_server)
  -> locked_out(until)
  -> cancelled(reason)
  -> stale(reason)
  -> timed_out(reason)
```

Every outcome must map to one explicit session stage.

### High — failure classification can mislabel server failure

Violates Standards §7.

`v1_api_login_user` returns a Boolean plus HTTP status. The auth worker treats a
failed request as wrong PIN whenever connectivity still appears online. This can
count a server 5xx as a credential failure. The product contract says network or
server failure goes to connecting and must not increment PIN failures.

Use a typed domain result rather than Boolean success:

```text
authenticated | wrong_credential | locked | unavailable | protocol_error
```

Transport reachability is insufficient to classify application response semantics.

### High — timing source and implementation disagree

Violates Standards §§7 and 12.

- Product contract: login verification uses the 2.5-second probe timeout.
- As-built: `V1_LOGIN_HTTP_MS` is a private 6000 ms literal.
- Boot hangout load uses 15000 ms.
- Wi-Fi join uses 25000 ms.

These values are absent from `shared/v1/timing.yaml` even though nearby retry
values are generated from it.

V2 should store policy in one typed configuration and verify every client surface
uses it.

### High — poller policy is encoded as incidental gates

Violates Standards §6.

`v1_state_may_probe()` returns `!v1_auth_login_active()`, and
`v1_connect_enter_from_signin()` also checks login activity. These guards address
the historical PIN-clobber race but do not define a general priority or merge
policy.

The next client should explicitly state:

- probe results defer while login is active;
- a confirmed link-down may request cancellation through the session owner;
- partial PIN preservation/clearing is decided by the operational contract;
- stale probe results carry their own operation ID.

### Medium — connectivity truth is one Boolean

Relates to Standards §6.

`s_server_online` conflates recent probe success, current reachability, and
application readiness. A richer but still small model would separate:

```text
link: down | joining | up
server: unknown | probing | ready | unavailable
freshness: timestamp
```

The UI derives its copy from session stage plus this truth; it does not transition
merely because a Boolean changed.

### Medium — persistent cache and readiness are tightly coupled

Relates to Standards §§6 and 9.

V1 correctly avoids showing cached roster while offline, but cache loading,
network refresh, roster ordering, profile parsing, and roster presentation all
live in `v1_connect.c`. This makes it easy for a future change to confuse available
cached data with confirmed readiness.

Separate repository/cache code from readiness policy.

## Performance priorities

1. Move Wi-Fi join, probes, and boot hangout fetch off the presentation owner.
2. Decouple dot animation from retry execution.
3. Make roster updates identity-keyed Level 1 changes.
4. Centralize typed timing and retry policy.
5. Replace direct state writes with one reducer and observable queue handling.

## Reliability tests for the next client

- Connecting dots remain smooth during a probe timeout.
- A probe failure during active login follows the documented defer/cancel policy.
- A stale successful login cannot authenticate after user or session changes.
- HTTP 401, 5xx, timeout, malformed response, and link-down produce distinct
  domain outcomes.
- Queue-full injection cannot leave `checking...` indefinitely.
- Every accepted login produces exactly one terminal event.
- Cached roster never produces false-ready UI.
- Retry timing is generated from one source on device and twin.

## Open product decisions

- Whether partial PIN is cleared or preserved on a transport interruption.
- Exact handling of non-credential 4xx responses and malformed login responses.
- Whether boot probe timeout intentionally differs from steady-state probe timeout.
- Which signed-out stage remains visible during transient link rejoin.

## Verification level of this assessment

This is source and contract analysis only. Historical device evidence motivated
the rules, but this document does not claim current target behavior was retested.
