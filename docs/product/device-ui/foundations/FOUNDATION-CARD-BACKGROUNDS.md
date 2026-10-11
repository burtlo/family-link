# Card background catalog (placeholder)

ID: `FOUNDATION-CARD-BACKGROUNDS`  
Type: Foundation  
Revision: 0.1  
Status: **Placeholder** — catalog not authored  
Owner: Product owner  

## Owner intent (recorded)

- Hangout users choose a **card background** from a fixed catalog (not free-form hex).
- Each catalog entry defines:
  - gradient or fill stops for the card face;
  - **`fl-color-text-on-card`** — text color chosen for **maximum contrast** on that background (accessibility-first).
- Roster and message cards use the user's selected entry; see [`COMP-USER-CARD`](../components/COMP-USER-CARD.md).

## Dependency

A separate specification will enumerate allowed backgrounds, preview swatches, and
server/profile fields. Until then, implementations **MUST NOT** invent ad hoc
per-user colors outside the future catalog.

## Revision history

- **0.1 — 2026-10-10:** Placeholder for owner-directed catalog + paired text colors.
