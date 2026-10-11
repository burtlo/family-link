# User profile communication contract

| Field | Value |
|---|---|
| ID | `COM-USER-PROFILE` |
| Revision | 0.1 |
| Status | V1 evidence plus v2 schema proposal; color meaning and persistence policy OPEN |
| Approval | No v2 profile or settings behavior approved by this document |
| Shared authority | [User-scoped envelope](user-scoped-envelope.md) |

## Boundary and v1 capture

`OP-PROFILE-READ`, `OP-PROFILE-UPDATE`, and a proposed `OP-PROFILE-HINT` synchronize server-owned user presentation settings. Device-local appearance belongs outside this contract unless the owner chooses to share it. The [v1 communication inventory](../../v1-assessments/communication-contract-inventory.md) is historical evidence.

| V1 exchange | As-built shape | Keep / adapt / replace |
|---|---|---|
| Update | `PUT /v1/profile` with optional `avatar_slot` integer 0–12, `accent_hex` string, and `autoplay_new` boolean; endpoint Bearer and caller-supplied `X-User-Id`. | **Retain profile fields as evidence, replace authority and concurrency model.** A stable avatar asset ID is preferable to an ordinal slot tied to one asset table. |
| Response | `{"ok":true,"profile":{"avatar_slot":number,"accent_hex":string,"autoplay_new":boolean}}`. The server persists profile state. It prepends `#` to a nonempty color without one and uppercases the value, but does not strictly validate six hex digits. | **Retain canonical read-after-write response**, add revision and explicit color validation. |
| Propagation | Profile appears in hangout, login, and inbox payloads. No dedicated profile-change push. | **Adapt** to explicit profile revision and change hint or documented refresh rule. |
| Device behavior | Icon/color change updates local state before sending the full profile; firmware ignores status/body. Browser twin shows failure toast but keeps optimistic local choice. | **Replace silent acknowledgement and stale whole-profile overwrite.** |
| Other colors | Browser card gradient is local storage; device card gradient is local setting. Sketch stroke color is per recorded stroke in the active sketch experience. | **Keep separate** from server-owned user profile until owner decides otherwise. |

## “Message color” needs a product definition

V1 `accent_hex` colors the user's presentation, including areas associated with messages. It does **not** freeze a color into each sent message. A future owner decision must choose whether existing messages follow the sender's latest profile color, capture the sender's color at send time, use a recipient-local card theme, or expose multiple distinct settings. The schema below calls this field `profile_accent` to avoid silently deciding that question. It is separate from sketch stroke color.

## Proposed checkable rules

| ID | Proposed invariant | V1 evidence / active source |
|---|---|---|
| `COM-001` | The server derives the editable user from the authenticated session; a supplied user ID cannot redirect a profile update. | V1 route trusts a separate roster-valid `X-User-Id`. |
| `COM-002` | An update changes only named fields, validates all of them, and returns canonical state with a new revision. | V1 whole-profile device save and canonical server response. |
| `COM-003` | A lost-response retry of the same operation returns the same result; stale revision fails without overwriting another endpoint's edit. | V1 has no revision or operation ID. |
| `COM-004` | The client distinguishes preview from server-saved profile and reconciles after a missed change hint. | V1 device ignores save result; twin keeps optimistic state after failure. |

## Proposed v2 schemas

### 1. Canonical profile — `GET /v2/me/profile` (proposed)

```json
{
  "schema": "fl.user-profile/2",
  "user_id": "user-a",
  "revision": 7,
  "avatar_id": "avatar-03",
  "profile_accent": "#FF6B6B",
  "autoplay_new": false,
  "updated_at": "2026-10-11T18:00:00Z"
}
```

| Field | Type and validation |
|---|---|
| `schema` | Required literal `fl.user-profile/2` in this proposal. |
| `user_id` | Required immutable user ID derived from authenticated session, not a writable target in a profile update. |
| `revision` | Required server-owned non-negative integer, incremented for each accepted change and durable across restart. |
| `avatar_id` | Required stable ID from a versioned catalog; must resolve on supported clients or have a defined fallback. V1 slots 0–12 are not automatically v2 IDs. |
| `profile_accent` | Required canonical uppercase `#RRGGBB` in this proposal; allowed palette versus arbitrary color remains OPEN. Its use on message cards remains OPEN. |
| `autoplay_new` | Optional in v2 if the feature remains; do not silently carry a v1 setting into an approved profile. |
| `updated_at` | Required server UTC timestamp for display/diagnostics; `revision`, not timestamp, orders changes. |

### 2. Partial update — `PATCH /v2/me/profile` (proposed)

```json
{
  "schema": "fl.user-profile-patch/2",
  "operation_id": "bfa8883e-a7f8-45bf-a2ef-cf2c7966ccf1",
  "expected_revision": 7,
  "changes": {"avatar_id": "avatar-04", "profile_accent": "#5AA0E8"}
}
```

`changes` must contain at least one allowed field; absent fields remain unchanged. Explicit `null` is invalid unless a field later defines a reset action. The server validates the full patch before changing any field, atomically persists it, increments revision once, and returns the full canonical `fl.user-profile/2`. Repeating the same `operation_id` and exact patch returns the first result; the same ID with different content returns `identity_conflict`. A stale `expected_revision` returns `revision_conflict` without a write and supplies or links to current profile state. Operation-key retention is OPEN.

### 3. Change hint — server → interested session (proposed)

```json
{
  "schema": "fl.profile-hint/2",
  "type": "profile_changed",
  "user_id": "user-a",
  "revision": 8
}
```

This is a hint to refetch a canonical profile or roster; it is not an authority to mutate local presentation by itself. Who receives it—other endpoints for the same user, all hangout members, or both—and how it is reconciled after a disconnect are OPEN. A `profile_accent` change must not be conflated with an inbox revision unless the chosen message-color policy requires it.

## Failure and continuity choices

| Condition | Proposed protocol behavior | Product behavior OPEN |
|---|---|---|
| Invalid avatar/color | `invalid_request` with field detail safe for logs; no partial change. | Picker allowed values and fallback. |
| Competing update from another endpoint | `revision_conflict`; client refetches before another edit. | Whether to merge independent fields or ask user. |
| Server unavailable after local choice | Do not claim saved without acknowledgement. | Keep optimistic preview, queue offline edit, or revert. |
| Response lost after commit | Retry same operation ID; replay first result. | How long client/server retain ID and pending choice. |
| Profile hint missed | Refetch on sign-in/reconnect or defined interval. | Visible update timing for other users and existing messages. |

| Scenario | Event | Proposed check | Remaining dependency |
|---|---|---|---|
| `COM-SCN-001` | Icon update is acknowledged, then another endpoint refreshes | Both see the same canonical avatar ID/revision. | COM-001–002; hint audience OPEN |
| `COM-SCN-002` | Save fails after local preview | UI does not claim saved; rollback/queue choice remains OPEN. | COM-004, COM-OPEN-004 |
| `COM-SCN-003` | Two endpoints edit from revision 7 | First succeeds; stale second gets conflict without overwriting first. | COM-002–003; merge policy OPEN |
| `COM-SCN-004` | Response lost after committed patch | Same operation ID replays first result and revision. | COM-003; retention OPEN |
| `COM-SCN-005` | Sender accent changes after a message is received | Visible result remains OPEN until “message color” meaning is decided. | COM-OPEN-001 |

| ID | Decision needed | Blocks |
|---|---|---|
| `COM-OPEN-001` | Define “message color” and whether color follows current profile, is frozen per message, or is recipient-local. | Field meaning and propagation |
| `COM-OPEN-002` | Decide server-owned versus device-local settings, including `autoplay_new` and card gradient. | Profile field set |
| `COM-OPEN-003` | Avatar catalog IDs/assets, allowed color values, fallback, and migration from v1 values. | Validation |
| `COM-OPEN-004` | Optimistic preview, save failure, offline edit, retry, and user-visible acknowledgement. | Update UX |
| `COM-OPEN-005` | Concurrent edit merge policy, idempotency retention, and profile hint audience/recovery. | Multi-endpoint consistency |

Revision 0.1 (2026-10-11): captured v1 profile request/response and proposed revisioned partial updates while leaving product meaning and propagation decisions OPEN.
