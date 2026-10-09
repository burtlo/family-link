# Agent guide — v1 POC archive (historical)

> **Superseded for active work.** Use the repo-root agent guide: [`../../docs/AGENTS.md`](../../docs/AGENTS.md) and [`../../docs/README.md`](../../docs/README.md). Agents must not modify this archive or link to `poc-v1/` paths in new artifacts outside the archive (see [`../../.cursor/rules/poc-v1-reference-archive.mdc`](../../.cursor/rules/poc-v1-reference-archive.mdc)).

This file describes **v1 as-built** inside the archive. Paths below are relative to `poc-v1/docs/` unless noted.

**Documentation index:** [`README.md`](README.md).

## Repository purpose (v1)

Desk **async audio mailbox** for a small hangout on **ESP32-S3-BOX-3** endpoints, with a **home-server** host (`demos/server/v1_product`) and Lynn **web admin**. Island demos (`h01`–`h31`, `p01`–`p13`) are experiments; **product glue** is **`firmware/v1/`** flashed as **`x02`**.

Do not treat v1 architecture as mandatory for a future rewrite. Product intent: [`product/vision.md`](product/vision.md). Approved contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md). As-built features: [`features/README.md`](features/README.md).

Portable standards and v1 assessments for **new** work live at repo root: [`../../docs/standards/client-application-coding-standards.md`](../../docs/standards/client-application-coding-standards.md) and [`../../docs/v1-assessments/`](../../docs/v1-assessments/carousel-playback.md).

## Build and test (from `poc-v1/`)

```bash
make v1-server          # product host (see demos/server/v1_product/README.md)
make demo-v1            # smoke
make x02 && make flash DEMO=x02
make v1-timing && make check-v1-parity   # shared/v1/timing.yaml
python3 -m unittest discover -s demos/server/v1_product/tests
```

USB serial (Mac): `/dev/cu.usbmodem*`. Flash via **box USB-C**, not dock power-only port.

## Cursor skills (repo root)

| Resource | Use |
|----------|-----|
| [`author-experience-specification`](../../.cursor/skills/author-experience-specification/SKILL.md) | Interview owner; write experience spec ([`../../docs/product/experience-specification-authoring.md`](../../docs/product/experience-specification-authoring.md)) |
| [`deep-implementation-retrospective`](../../.cursor/skills/deep-implementation-retrospective/SKILL.md) | Evidence-based retrospective |

Legacy v1 firmware Cursor rules (`device-verify-before-done`, `lvgl-incremental-ui`, etc.) are not carried forward at repo root; use [`../../docs/standards/`](../../docs/standards/client-application-coding-standards.md) for portable guidance.

Stability synthesis: [`../stability-synthesis.md`](../stability-synthesis.md).

## Experiments vs product

- **Experimental:** demo ids, storage qual epochs, long-message host routes without firmware adoption, personality demos.
- **Product:** x02 + v1_product server paths described in [`features/`](features/).

When stuck: [`decisions/open-questions.md`](decisions/open-questions.md).
