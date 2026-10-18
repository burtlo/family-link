# LAB-10 review: scripted canvas appears white-only

- Review run: `20261018-L10-RECORD-CANVAS-020`
- Target: `/Users/lynnfrank/src/family-link/client/labs/lab-10-record-canvas/`
- Scope: current scripted palette-change to visible-canvas path
- Branch / HEAD: `main` / `66086bc5023af7cb394a4c35d1ea95d3342cf9b0`
- Mode: review, implement, independently verify; implementation authorized by
  the user earlier in this task
- WIP: dirty worktree preserved; no source edits made during review
- Source provenance: `main/lab10_ui_lvgl.c`
  `75ec93280a729006737ab7c9f57ee9e615e5f39636108e4796b6f3233e091d6a`;
  `components/lab10_ui/lab10_ui.c`
  `56fc1235c3fcee3d9660730bdf4d8a71484b3c7e59b1fabd51593e2bbe555d4d`;
  `components/lab10_capture/lab10_capture.c`
  `726405d2ad3a6d76d82a2d1b1be867cdadd0042d00874d0fa0dab75b2238a517`;
  `tests/test_lab10_scripted.c`
  `5426c2b1c133a0091b108a4afcd6dd950476a504ce060a698df4a9a7710b5d74`;
  combined tracked diff hash
  `e06180c7639ad1c32376b774576da59a5f5d994ecc5f9ca470f1b1caf869a63b`.

## Applicable sources

| Source | Authority and relevant requirement | Affected path |
|---|---|---|
| `docs/standards/client-application-coding-standards.md` | Normative; §§5, 11, 12 require correct UI updates, evidence-backed claims, and avoiding speculative changes | Renderer / LVGL canvas / verification |
| `client/labs/lab-10-record-canvas/manifest.lab.json` | Active LAB-10 contract; palette selection applies to following accepted points and the selected color is retained in capture evidence | Palette, capture, render |
| `docs/product/experiences/sketch-record-and-playback.md` | Owner-decided multi-color drawing intent, SK-006 | Canvas behavior |
| `client/labs/lab-10-record-canvas/README.md` | Lab evidence boundary; visual/scanout claims require synchronized target evidence | Scripted run and visual confirmation |

No relevant OPEN product decision or source conflict was identified.

## Finding LAB10-F011

- Classification / priority: confirmed user-observed runtime defect, P2
- Trigger: run `lab.lab10.scripted`; the user reports the canvas is visibly
  drawing only white and never appears to change color.
- Effect: scripted multicolor drawing intent is not visible on the target.
- Requirement: palette changes must affect subsequent accepted stroke points and
  those colors must reach the displayed canvas (manifest palette contract and
  experience SK-006).
- Source trace: scripted palette events use swatch centers in
  `components/lab10_ui/lab10_ui.c`; human and scripted palette events use the
  same model handler; selected indices are submitted to capture and stored on
  accepted points; the renderer preserves point color in raster/publication
  events; `main/lab10_ui_lvgl.c` maps the index through `lab10_color_rgb` and
  writes the color with `lv_canvas_set_px`.
- Source outcome: no color-index mismatch was located in this path. Existing
  firmware diagnostics measure accepted points/color mask, not the canvas
  buffer or display scanout. The first point of divergence is unknown.
- Smallest fix: not yet justified. First add bounded diagnostics that distinguish
  renderer-published colors from colors observed in the actual native-format
  canvas buffer; patch only if this identifies a code boundary.
- Verification: exercise the real scripted scheduler/capture/renderer path and
  record a target run with the new diagnostics. A claim that the screen itself
  is corrected additionally requires synchronized target visual evidence.
- Evidence: the user's live observation is L3 evidence of the symptom, but no
  run ID, binary fingerprint, serial output, or synchronized image is attached.
  Historical run `20261018-scripted-run-5d7adf6d` reports accepted color mask
  `0x3e`; it is not evidence of displayed color. The current cause is
  unverified.

## Ownership / wait map

```text
scripted timer ─┐
                ├→ UI model → capture queue/selected color → accepted point
human touch ────┘                                             ↓
                                                renderer raster/publication
                                                             ↓
                                                LVGL canvas native buffer
                                                             ↓
                                                       LCD scanout
```

Palette events and strokes are serialized by the UI task. Capture owns accepted
point color; renderer publishes color-bearing pixel events; LVGL owns the canvas
buffer and invalidation. The current diagnostic records the capture stage only.

## Execution plan

1. Instrument renderer publication and native canvas-buffer readback with bounded
   per-color masks in the existing scripted terminal diagnostic, and add focused
   tests for the real scripted event-to-render publication path. Keep instrumentation
   outside per-pixel hot work except for the first sample of each color. Do not
   change palette or drawing semantics without evidence.
2. Stop source edits and send the exact diff, raw test output, and source state to
   a fresh independent read-only verifier. If the verifier finds a regression,
   allow up to three focused implementation attempts as authorized by the user.
3. After verification and when the current device run is no longer active, run a
   new scripted target capture. Compare accepted-color, published-color, and
   canvas-buffer masks. Localize and fix any first divergence; obtain visual
   target confirmation before claiming the white-only screen defect fixed.

There is one required dependency: target-side color diagnostics must exist and
pass independent verification before a diagnostic capture can localize the
runtime boundary. No unrelated work is in this plan.

## Review commands and results

- Read `docs/AGENTS.md`, the complete client coding standard, LAB-10 README and
  manifest, and the active experience SK-006 source.
- Inspected the scripted palette scheduler, model routing, capture point creation,
  renderer raster/publication, LVGL pixel conversion and write, and historical
  diagnostic evidence.
- Confirmed the relevant working-tree hashes listed above.
- No build, test, target access, flashing, screen capture, or source edit was
  performed by the read-only reviewer.

## Outcome

`LAB10-F011`: the reported white-only symptom is resolved on the freshly
flashed scripted run `20261018-scripted-run-4afc6192`. The user confirmed that
multiple colors were visible after that run. Device diagnostics independently
show all five ink colors accepted, published, sampled from the LVGL canvas
buffer, and matched to expected RGB565 (`0x3e` masks), with the renderer
drained and zero queue/trace losses. Workflow is `PASS`; experiment verdict
remains `UNMEASURED`. The original symptom's cause is unlocalized, and no
synchronized screen recording was captured. A final post-overdraw canvas-color
mask diagnostic was added after the target run; a fresh independent verifier
found no correctness or build/test regression. The LAB-10 Python suite (68),
manual and scripted builds, and scoped diff check passed. The final-mask field
itself has not been exercised on hardware. Its one-shot scan performs 14,400
canvas reads in the 20 ms UI polling task; target latency is unmeasured, so a
brief completion-time UI pause remains possible. No commit yet.
