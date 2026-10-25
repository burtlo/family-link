# Family Link documentation (active)

Entry point for **new product work**. The v1 proof-of-concept is frozen under `poc-v1/` (read-only reference for agents; see [`AGENTS.md`](AGENTS.md)).

## Agent boundaries

Read [`AGENTS.md`](AGENTS.md) before inventing product behavior or touching code (includes shell conventions for Windows PowerShell and macOS zsh).

## Product workflow

| Doc | Role |
|-----|------|
| [`product/experience-specification-authoring.md`](product/experience-specification-authoring.md) | Interview workflow and spec structure for new experiences |
| `docs/product/experiences/<slug>.md` | Owner-approved experience specifications (create as needed) |
| [`product/device-ui-specification-authoring.md`](product/device-ui-specification-authoring.md) | Workflow and required structure for fixed-hardware layouts, components, and screens |
| [`product/device-ui/`](product/device-ui/README.md) | V2 device UI foundations, reusable layouts, components, and screen compositions (including [`FOUNDATION-PROFILE-AVATARS`](product/device-ui/foundations/FOUNDATION-PROFILE-AVATARS.md)) |
| [`product/device-ui/catalog/`](product/device-ui/catalog/README.md) | YAML catalog of those layouts; flash with `make lab.lab14` |
| [`product/communication-contract-authoring.md`](product/communication-contract-authoring.md) | Reusable client/server contract format and review questions |
| [`product/communication-contracts/endpoint-health-and-login.md`](product/communication-contracts/endpoint-health-and-login.md) | Draft v2 health, roster, and PIN exchange contract; blocking wire decisions recorded |
| [`product/communication-contracts/user-scoped-envelope.md`](product/communication-contracts/user-scoped-envelope.md) | Proposed signed-in authority and shared error schema |
| [`product/communication-contracts/message-delivery.md`](product/communication-contracts/message-delivery.md) | Draft v2 send exchange; atomic frozen-set, keep/resume, recipients-only publication, and refusal-vs-wait classes recorded; remaining work is engineering wire |
| [`product/communication-contracts/inbox-and-media.md`](product/communication-contracts/inbox-and-media.md) | V1 inbox/media capture and proposed v2 sync, fetch, and playback-state schemas |
| [`product/communication-contracts/user-profile.md`](product/communication-contracts/user-profile.md) | V1 icon/color capture and proposed v2 profile schemas |
| [`plans/`](plans/README.md) | Feature plan bundles. The [V2 core-journey map](plans/v2-experience/implementation.md) is a preview, not orchestration-ready |

Historical v1 product docs (vision, requirements, plans, features) remain in the **v1 POC archive** until migrated into `docs/product/`.

## Client engineering

Product device build and flash: [device-build-flash](standards/device-build-flash.md)
(bootstrap marker `V2_BOOTSTRAP_READY` vs product `product_ui_ready`).

| Doc | Role |
|-----|------|
| [`standards/client-application-coding-standards.md`](standards/client-application-coding-standards.md) | Portable rules for the next client generation |
| [`standards/device-target-invariants.md`](standards/device-target-invariants.md) | Product `client/device/` invariants, claim classes, and mechanical guardrail map |
| [`standards/device-presentation-invariants.md`](standards/device-presentation-invariants.md) | Product `client/device/` tick order, single screen owner, touch and display-power rules, desk matrix |
| [`standards/device-build-flash.md`](standards/device-build-flash.md) | `make app:build` / flash, boot markers, verification ladder |
| [`standards/client-code-review-agent.md`](standards/client-code-review-agent.md) | Orchestrated implementation review, dependency plan, sequential fixes, and independent verification (IDs are per-report); skill `client-code-review` |
| [`standards/project-configuration.md`](standards/project-configuration.md) | `config.host` / `config.deployment` / `config.labs`, commands, flows, host INI |
| [`standards/deployment-configuration.md`](standards/deployment-configuration.md) | Desk roster YAML schema and validation |
| [`standards/labs-configuration.md`](standards/labs-configuration.md) | Device labs host registry (`config/labs.registry.yaml`) and `make lab.*` |
| [`v1-assessments/carousel-playback.md`](v1-assessments/carousel-playback.md) | Carousel, focus, playback (v1 evidence) |
| [`v1-assessments/authentication-connectivity.md`](v1-assessments/authentication-connectivity.md) | Wi‑Fi, roster, PIN, login (v1 evidence) |
| [`v1-assessments/recording-send.md`](v1-assessments/recording-send.md) | Capture, upload, receipt (v1 evidence) |
| [`v1-assessments/communication-contract-inventory.md`](v1-assessments/communication-contract-inventory.md) | V1 evidence and remaining v2 send, inbox, media, and profile contract boundaries |
| [`v1-assessments/async-operation-envelope.md`](v1-assessments/async-operation-envelope.md) | Operation identity, cancellation, terminal outcomes |
| [`v1-assessments/ui-update-taxonomy.md`](v1-assessments/ui-update-taxonomy.md) | Focus, selection, presentation identity |
| [`v1-assessments/session-stage-vs-background-poller.md`](v1-assessments/session-stage-vs-background-poller.md) | Stage vs dependency truth |
| [`v1-assessments/operational-contract-shape.md`](v1-assessments/operational-contract-shape.md) | Stages, failures, retries, timeouts |
| [`v1-assessments/resource-budget-envelope.md`](v1-assessments/resource-budget-envelope.md) | Limits and measurement |
| [`v1-assessments/v2-implementation-practices.md`](v1-assessments/v2-implementation-practices.md) | Referenced synthesis of v1, demos, and x02 with v2 implementation gates |
| [`demos/README.md`](demos/README.md) | Island demo **execution** catalog (canonical proofs outside `LAB-*`; includes [Opus E2E h31](demos/opus-chunk-end-to-end-h31.md)) |
| [`labs/README.md`](labs/README.md) | Lab index (`LAB-01`–`LAB-16`), links to layout and assistant guide |
| [`labs/repository-layout.md`](labs/repository-layout.md) | Projected `client/labs/`, `server/labs/`, build, artifacts, and evidence paths |
| [`labs/lab-02-implementation-outline.md`](labs/lab-02-implementation-outline.md) | LAB-02 tree, phases, verification scenarios, milestones |
| [`labs/assistant-guide.md`](labs/assistant-guide.md) | Workflow for agents and humans implementing or running labs |
| [`labs/inbox-carousel-playback-evaluation.md`](labs/inbox-carousel-playback-evaluation.md) | Proposed device labs, capture requirements, and decision triggers for large inboxes and audio/sketch playback |
| [`labs/v2-experience-demo-roadmap.md`](labs/v2-experience-demo-roadmap.md) | Additional new demos for login, session, capture/send, settings, and the composed v2 journey |

## Cursor / Codex skills

| Skill | Role |
|-------|------|
| [`.cursor/skills/author-experience-specification/SKILL.md`](../.cursor/skills/author-experience-specification/SKILL.md) | Author or revise experience specs |
| [`.cursor/skills/author-device-ui-specification/SKILL.md`](../.cursor/skills/author-device-ui-specification/SKILL.md) | Author or revise fixed-hardware device UI specs |
| [`.cursor/skills/author-communication-contract/SKILL.md`](../.cursor/skills/author-communication-contract/SKILL.md) | Author or revise client/server communication contracts |
| [`.cursor/skills/deep-implementation-retrospective/SKILL.md`](../.cursor/skills/deep-implementation-retrospective/SKILL.md) | Evidence-based engineering retrospective |

Mirrors under `.agents/skills/` for Codex.
