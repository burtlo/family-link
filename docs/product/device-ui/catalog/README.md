# Device UI catalog (machine-readable)

Status: draft companion to the Markdown specifications. This catalog does **not**
approve visuals or replace experience behavior.

The Markdown artifacts under [`docs/product/device-ui/`](../README.md) remain
the review surface. These YAML files are the same composition model in a form
the device can mount:

```text
scene
└─ layout (LAYOUT-SPLIT-PANE, LAYOUT-SPLIT-PANE-FOCUSED-SIDE, or catalog LAYOUT-FULL-CANVAS)
   ├─ upper / focused → PANEL-* + props
   └─ lower / side    → PANEL-* + props
```

[`LAB-14`](../../../../client/labs/lab-14-device-ui-catalog/README.md) packs this
directory at firmware build, parses the result on the BOX-3, and paints each
scene with the registered panel renderers.

## What is data vs code

| Change | Edit | Reflash? |
|---|---|---|
| New screen that reuses existing panels | Add `scenes/<slug>.yaml` with the next unused `CAT-NN` | Yes today (embedded pack). Serial `scene CAT-NN` switches among packed scenes without rebuild |
| Copy, fixture users, PIN marks, button names | Scene `props` | Same |
| Split height, margins, card size, theme hex | `tokens.yaml` | Yes today; renderers read tokens at mount |
| New `PANEL-*` or `COMP-*` | Markdown spec + C renderer + a scene that mounts it | Yes |

Live YAML push without rebuild is the intended next step (serial/HTTP replace of
the packed catalog). The renderer is already data-driven; only the transport is
missing.

## Files

| Path | Role |
|---|---|
| `catalog.yaml` | Catalog identity and theme |
| `tokens.yaml` | Canvas, shell, theme, and measured panel geometry |
| `scenes/*.yaml` | One visual state each (`id: CAT-NN`) |
| `schema/device-ui-catalog.v1.json` | Packer validation |

## Scene IDs

Use **`CAT-NN`** when talking about a visual on the device. The catalog index
prefixes every row with this ID, and a corner badge repeats it on the mounted
scene. Do not reuse a number; the next unused ID is `CAT-20`.

| ID | Index title | Screen |
|---|---|---|
| `CAT-01` | Sign in as ... | `SCR-SIGN-IN-ROSTER` |
| `CAT-02` | One-user roster | `SCR-SIGN-IN-ROSTER` |
| `CAT-03` | Eight-user roster | `SCR-SIGN-IN-ROSTER` |
| `CAT-04` | Updating hangout ... | `SCR-SIGN-IN-ROSTER` |
| `CAT-05` | Roster + connecting | `SCR-SIGN-IN-ROSTER` |
| `CAT-06` | Roster + no Wi-Fi | `SCR-SIGN-IN-ROSTER` |
| `CAT-07` | Loading hangout ... | `SCR-EMPTY-HANGOUT` |
| `CAT-08` | No people in this hangout | `SCR-EMPTY-HANGOUT` |
| `CAT-09` | Empty hangout + connecting | `SCR-EMPTY-HANGOUT` |
| `CAT-10` | PIN entry | `SCR-PIN-ENTRY` |
| `CAT-11` | PIN checking | `SCR-PIN-ENTRY` |
| `CAT-12` | Wrong PIN | `SCR-PIN-ENTRY` |
| `CAT-13` | PIN + connecting | `SCR-PIN-ENTRY` |
| `CAT-14` | PIN cooldown | `SCR-PIN-COOLDOWN` |
| `CAT-15` | Title + four buttons | gallery fixture |
| `CAT-16` | Title + action + two buttons | gallery fixture |
| `CAT-17` | Record sketch | `SCR-RECORD-SKETCH` |
| `CAT-18` | Record sketch, black selected | `SCR-RECORD-SKETCH` |
| `CAT-19` | Record sketch, gold selected | `SCR-RECORD-SKETCH` |

The badge is catalog chrome for review, not a product component.

Host pack:

```text
python scripts/device_ui_catalog.py validate
python scripts/device_ui_catalog.py pack
```

## Catalog-only stand-ins

These are labeled review fixtures, not owner-approved product policy, except
where a current foundation explicitly adopts them:

- the eight card-face fills and paired text colors are adopted by [`FOUNDATION-CARD-BACKGROUNDS`](../foundations/FOUNDATION-CARD-BACKGROUNDS.md);
- circular initial avatars are the owner-approved interim fallback until the new [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md) artwork is ready; V1 avatar art is not reused;
- two-pane surfaces `#101418` / `#242C34` and control fill `#3A444E` so buttons stay visible on the lower pane;
- 8 px button radius where panel chrome is still `UI-OPEN`;
- keypad dim at 50% opacity while `SCR-PIN-ENTRY` `UI-OPEN-001` is open;
- `LAYOUT-FULL-CANVAS` for [`SCR-PIN-COOLDOWN`](../screens/SCR-PIN-COOLDOWN.md) until the missing layout spec is authored;
- `CAT-NN` corner badge and index prefix, catalog chrome for naming a scene.
  On record-sketch scenes the badge sits top-left so it does not cover the palette.

Built-in LVGL Montserrat 16/24/32 is the current glyph stand-in for weight 600
(`FOUNDATION-DEVICE-CANVAS` `UI-OPEN-001`).
