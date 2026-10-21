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
| [`product/device-ui/`](product/device-ui/README.md) | V2 device UI foundations, reusable layouts, components, and screen compositions |
| [`product/device-ui/catalog/`](product/device-ui/catalog/README.md) | YAML catalog of those layouts; flash with `make lab.lab14` |
| [`product/communication-contract-authoring.md`](product/communication-contract-authoring.md) | Reusable client/server contract format and review questions |
| [`product/communication-contracts/endpoint-health-and-login.md`](product/communication-contracts/endpoint-health-and-login.md) | Draft v2 health, roster, and PIN exchange contract; blocking wire decisions recorded |
| [`product/communication-contracts/user-scoped-envelope.md`](product/communication-contracts/user-scoped-envelope.md) | Proposed signed-in authority and shared error schema |
| [`product/communication-contracts/message-delivery.md`](product/communication-contracts/message-delivery.md) | V1 send wire capture and proposed v2 delivery schemas |
| [`product/communication-contracts/inbox-and-media.md`](product/communication-contracts/inbox-and-media.md) | V1 inbox/media capture and proposed v2 sync, fetch, and playback-state schemas |
| [`product/communication-contracts/user-profile.md`](product/communication-contracts/user-profile.md) | V1 icon/color capture and proposed v2 profile schemas |

Historical v1 product docs (vision, requirements, plans, features) remain in the **v1 POC archive** until migrated into `docs/product/`.

## Client engineering

Device bootstrap commands: [build and flash](standards/device-build-flash.md).

| Doc | Role |
|-----|------|
| [`standards/client-application-coding-standards.md`](standards/client-application-coding-standards.md) | Portable rules for the next client generation |
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
| [`labs/README.md`](labs/README.md) | Lab index (`LAB-01`–`LAB-13`), links to layout and assistant guide |
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
