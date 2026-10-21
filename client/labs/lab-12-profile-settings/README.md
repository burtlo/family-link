# LAB-12 — Profile settings propagation

Version: `LAB-12/v1 = firmware lab12-fw-<variant>-v1 + server LAB-12/v1 + fixture lab12-profile/v1`.
Target: ESP32-S3 BOX-3, 320 × 240 RGB565. The profile wire shapes and UI are lab
fixtures; they do not approve product behavior or a visual design.

## Operator commands

Visual, with no server or port binding:

```sh
make lab.lab12
make lab.lab12.scripted
```

Shared profile service (owned service on `127.0.0.1:8771`):

```sh
make lab.lab12 VARIANT=shared KIT=<kit>
make lab.lab12.scripted VARIANT=scripted-shared KIT=<kit>
```

Build each isolated variant:

```sh
make lab.lab12.build
make lab.lab12.build VARIANT=scripted
make lab.lab12.build VARIANT=shared
make lab.lab12.build VARIANT=scripted-shared
```

On-screen guidance: Settings exposes profile accent and avatar previews, Save,
Cancel, and a separate local sketch stroke-color swatch. A preview is not saved
until an acknowledged write; Cancel discards it without success copy. A rejected
write must not appear saved. The detail overlay's Close returns to the same card
focus. Back returns to the same three-card static inbox. Accent edits do not
recolor those cards or change sketch stroke color. The operator guide above
explains each control and the expected success/cancel/failure outcome.

## Scope and resource probe

The `split-pane` geometry is an exploratory P-layout fixture copied from the
read-only LAB-06 geometry prerequisite: upper 320×80, lower 320×160. It has no
visual approval. The `full-canvas` fixture arm is omitted. Rejected-preview
policy is `rollback`; offline work is `retained`; message-color observations are
`follow-profile` and `frozen-on-message`. All four are exploratory observations.
COM-OPEN-001–005 remain OPEN. No server token is a credential.

`ResourceBudget` (provisional lab probe): max_live_nodes=48,
max_transient_bytes=16384, max_persistent_bytes=0, max_concurrent_operations=1,
max_stack_or_call_depth=8, maximum_input_size=2048 bytes. The modeled save-result
queue capacity is 4; submit overflow is explicit failure. These numbers are not
hardware measurements. Presentation callbacks update the local preview and
enqueue one immutable shared update. A dedicated shared UI worker owns Save
HTTP requests; the scripted harness uses the same fixture client synchronously
for scenario probes. The job and result queues each hold one operation. Visual
variants do not initialize networking.

## Trace and scenarios

The owned parser is `tools/parse_lab_trace.py`; schema is
`trace-schema.v1.json`. Device rows have one terminal per operation ID. The
visual decided inputs are exactly `P-preview`, `P-modal`, `P-return`. Shared
decided inputs are exactly `P-save-fail`, `P-conflict`, `P-lost`, `P-invalid`,
`P-hint`. Exploratory rows `P-save-policy`, `P-offline`, `P-message-color`, and
`P-layout` retain their observation and never enter either verdict array.

## Evidence limits

Builds and host/model checks establish L0/L1 only. User-supplied target captures
exist, but the latest scripted-shared run is workflow PASS / experiment FAIL;
no passing target run, new target run after the fixture fix, visual review,
timing sample, or product decision is claimed. Performance is UNMEASURED;
device criteria L2/L3 are pending. The visual screen remains an exploratory
settings prototype, not SCR-SETTINGS approval.
