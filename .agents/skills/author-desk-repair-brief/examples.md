# Example — login Wave 1 PIN follow-up (condensed)

## Operator input (minimal)

```
FAIL: Wrong pin shows "wrong pin", then second pin → checking → login screen. Intermittent.
PASS: Mute from connecting recovers roster; mute cycle can login again.
Telemetry: pin_login 409 identity_conflict on …000000000008 then roster 200.
```

## Operator summary (skill output A)

- **Primary:** EXP-LOGIN; contract `endpoint-health-and-login.md` COM-006, COM-015, COM-020.
- **Hypothesis:** Second submit hits 409 → `fail_pin_operation_refused` → roster; or stale server `pin_records` with low sequential op ids before mint fix.
- **Not in scope:** Wave 2 PRES-PATH / INT-WIRE; unrelated `make test` failures.
- **Desk tip:** Restart desk server if 409 on “first” submit; capture telemetry on **second** submit.

## Session plan excerpt (skill output B — in chat)

See [session-plan.md](session-plan.md). Key fields:

- **Mode:** Review, implement, and verify
- **Tasks:** (1) host test wrong_pin then authenticated same user (2) cancel stale PIN HTTP jobs (3) refuse UX vs COM-015
- **Wave exit:** wrong pin → retry → inbox without mute, three times
- **First action when you say go:** run named host model tests, then one desk repro with telemetry

## Invitation (skill output C)

> This is the scope and order I’ll follow. Say **go** when you want me to start
> fixes in this chat.

After **go**, the agent runs client-code-review discipline in the same session
(no second chat required).
