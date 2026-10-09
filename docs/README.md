# Family Link documentation

Entry point for humans and agents. **Product truth**, **implementation truth**, and **historical evidence** live in different places — do not treat the v1 codebase as the specification for the next product generation.

## 1. What Family Link is

| Doc | Role |
|-----|------|
| [`product/vision.md`](product/vision.md) | Goals and user experience (product-centered) |
| [`product/requirements.md`](product/requirements.md) | v1 merge requirements (summary) |
| [`plans/v1-product-spec.md`](plans/v1-product-spec.md) | Approved v1 contract |
| [`BOX-UI.md`](BOX-UI.md) | Endpoint screen and interaction brief |

## 2. Hardware

**Index:** [`hardware/README.md`](hardware/README.md)

| Doc | Role |
|-----|------|
| [`hardware/overview.md`](hardware/overview.md) | BOX-3 platform summary (start here) |
| [`hardware/capabilities.md`](hardware/capabilities.md) | Spec vs observed vs inferred |
| [`hardware/storage-summary.md`](hardware/storage-summary.md) | On-chip vs SD; qualification status (short) |
| [`hardware/audio.md`](hardware/audio.md) | Mic, speaker, recording (short) |
| [`hardware/networking.md`](hardware/networking.md) | Wi‑Fi, TLS, remote (short) |
| [`hardware/limitations.md`](hardware/limitations.md) | Known limits and retired experiments |
| [`hardware/HARDWARE.md`](hardware/HARDWARE.md) | Detailed specs vs requirements |
| [`hardware/STORAGE.md`](hardware/STORAGE.md) | Shopping, failure modes, outbox design |
| [`hardware/ATTACHED-STORAGE.md`](hardware/ATTACHED-STORAGE.md) | H35/H37/H38 operator guide |
| [`hardware/SPEAKER.md`](hardware/SPEAKER.md) | Audio I/O, Pmod wiring |
| [`hardware/UVC-CAMERA.md`](hardware/UVC-CAMERA.md) | Dock USB camera |

## 3. What works today (v1 as-built)

| Doc | Role |
|-----|------|
| [`features/README.md`](features/README.md) | Feature index and status |
| [`architecture/overview.md`](architecture/overview.md) | Endpoint ↔ server ↔ web |
| [`architecture/server.md`](architecture/server.md) | `demos/server/v1_product` |
| [`architecture/client.md`](architecture/client.md) | `firmware/v1` + web twin |
| [`architecture/protocols.md`](architecture/protocols.md) | Current wire behavior |

Run host: `make v1-server` · Flash box: `make x02` · Tests: `python3 -m unittest discover -s demos/server/v1_product/tests`

## 4. Client engineering standards and v1 assessments

The standards are portable guidance for the next client generation. The
assessments explain the v1 evidence behind them without making v1 architecture
normative.

| Doc | Role |
|-----|------|
| [`standards/client-application-coding-standards.md`](standards/client-application-coding-standards.md) | Presentation/session/I/O boundaries, async operations, UI lifecycle, resource budgets, and verification |
| [`v1-assessments/carousel-playback.md`](v1-assessments/carousel-playback.md) | Carousel, focus, playback, and inbox refresh assessment |
| [`v1-assessments/authentication-connectivity.md`](v1-assessments/authentication-connectivity.md) | Wi-Fi, server readiness, roster, PIN, and login assessment |
| [`v1-assessments/recording-send.md`](v1-assessments/recording-send.md) | Recipient picker, capture, upload, and receipt assessment |

Expanded concept guides with concrete v1 examples:

- [`v1-assessments/async-operation-envelope.md`](v1-assessments/async-operation-envelope.md)
- [`v1-assessments/ui-update-taxonomy.md`](v1-assessments/ui-update-taxonomy.md)
- [`v1-assessments/session-stage-vs-background-poller.md`](v1-assessments/session-stage-vs-background-poller.md)
- [`v1-assessments/operational-contract-shape.md`](v1-assessments/operational-contract-shape.md)
- [`v1-assessments/resource-budget-envelope.md`](v1-assessments/resource-budget-envelope.md)

## 5. What remains to build

| Doc | Role |
|-----|------|
| [`plans/README.md`](plans/README.md) | Ordered backlog (product-no-storage, storage follow-on, long-message, hardening) |
| [`plans/product-no-storage-roadmap.md`](plans/product-no-storage-roadmap.md) | Active product track |

Normative **future** messaging (not x02 as-built): [`MESSAGE-PROTOCOL.md`](MESSAGE-PROTOCOL.md), [`LONG-MESSAGE-ARCHITECTURE.md`](LONG-MESSAGE-ARCHITECTURE.md).

## 6. Decisions and evidence

| Doc | Role |
|-----|------|
| [`decisions/README.md`](decisions/README.md) | ADRs and open questions index |
| [`decisions/open-questions.md`](decisions/open-questions.md) | Unresolved product/tech choices |
| [`evidence/`](evidence/) | Qualification runs, host tests, partition research |

Device incident catalog: [`decisions/device-incidents.md`](decisions/device-incidents.md) (links repo-root [`../stability-synthesis.md`](../stability-synthesis.md)).

## 7. Experiments and demos

| Doc | Role |
|-----|------|
| [`DEMO-MAP.md`](DEMO-MAP.md) | Built demos and audio paths |
| [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) | Firmware island plan |
| [`SERVER-DEMOS.md`](SERVER-DEMOS.md) | Host protocol islands |
| [`plans/v1-demo-set.md`](plans/v1-demo-set.md) | v1 verification runbook |

## 8. Agents working in this repo

[`AGENTS.md`](AGENTS.md) — short operational guide (build, boundaries, where to update docs).
