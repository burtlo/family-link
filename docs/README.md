# Family Link documentation (active)

Entry point for **new product work**. The v1 proof-of-concept is frozen under `poc-v1/` (read-only reference for agents; see [`AGENTS.md`](AGENTS.md)).

## Agent boundaries

Read [`AGENTS.md`](AGENTS.md) before inventing product behavior or touching code (includes shell conventions for Windows PowerShell and macOS zsh).

## Product workflow

| Doc | Role |
|-----|------|
| [`product/experience-specification-authoring.md`](product/experience-specification-authoring.md) | Interview workflow and spec structure for new experiences |
| `docs/product/experiences/<slug>.md` | Owner-approved experience specifications (create as needed) |

Historical v1 product docs (vision, requirements, plans, features) remain in the **v1 POC archive** until migrated into `docs/product/`.

## Client engineering

| Doc | Role |
|-----|------|
| [`standards/client-application-coding-standards.md`](standards/client-application-coding-standards.md) | Portable rules for the next client generation |
| [`standards/project-configuration.md`](standards/project-configuration.md) | `config.host` / `config.deployment`, commands, flows, host INI |
| [`standards/deployment-configuration.md`](standards/deployment-configuration.md) | Desk roster YAML schema and validation |
| [`v1-assessments/carousel-playback.md`](v1-assessments/carousel-playback.md) | Carousel, focus, playback (v1 evidence) |
| [`v1-assessments/authentication-connectivity.md`](v1-assessments/authentication-connectivity.md) | Wi‑Fi, roster, PIN, login (v1 evidence) |
| [`v1-assessments/recording-send.md`](v1-assessments/recording-send.md) | Capture, upload, receipt (v1 evidence) |
| [`v1-assessments/async-operation-envelope.md`](v1-assessments/async-operation-envelope.md) | Operation identity, cancellation, terminal outcomes |
| [`v1-assessments/ui-update-taxonomy.md`](v1-assessments/ui-update-taxonomy.md) | Focus, selection, presentation identity |
| [`v1-assessments/session-stage-vs-background-poller.md`](v1-assessments/session-stage-vs-background-poller.md) | Stage vs dependency truth |
| [`v1-assessments/operational-contract-shape.md`](v1-assessments/operational-contract-shape.md) | Stages, failures, retries, timeouts |
| [`v1-assessments/resource-budget-envelope.md`](v1-assessments/resource-budget-envelope.md) | Limits and measurement |

## Cursor / Codex skills

| Skill | Role |
|-------|------|
| [`.cursor/skills/author-experience-specification/SKILL.md`](../.cursor/skills/author-experience-specification/SKILL.md) | Author or revise experience specs |
| [`.cursor/skills/deep-implementation-retrospective/SKILL.md`](../.cursor/skills/deep-implementation-retrospective/SKILL.md) | Evidence-based engineering retrospective |

Mirrors under `.agents/skills/` for Codex.
