# Agent guide — Family Link (active repo)

**Documentation index:** [`README.md`](README.md). Read it before inventing product behavior.

## Repository layout

| Area | Role |
|------|------|
| [`docs/`](README.md) | Active product documentation, standards, v1 assessments, experience-spec workflow |
| [`client/device/`](../client/device/) | Product v2 firmware (bootstrap today) |
| [`client/labs/`](../client/labs/) | **Projected** per-lab ESP-IDF projects — see [`docs/labs/repository-layout.md`](labs/repository-layout.md) |
| [`server/labs/`](../server/labs/) | **Projected** lab fixture servers |
| [`../config/host.defaults.ini`](../config/host.defaults.ini) | Host commands, flows, and toolchain defaults |
| [`../config/deployment/`](../config/deployment/example.yaml) | Product roster (example + gitignored `local.yaml`) |
| [`../config/labs.registry.yaml`](../config/labs.registry.yaml) | Device labs host registry (`make lab.<name>`) |
| `poc-v1/` | **Read-only** v1 POC archive (firmware, demos, legacy docs). Reference only — see below |
| [`.cursor/rules/poc-v1-reference-archive.mdc`](../.cursor/rules/poc-v1-reference-archive.mdc) | Enforces archive boundaries for agents |

## poc-v1 archive (reference only)

The v1 proof-of-concept lives under `poc-v1/`. Agents **must not** modify it or link to its paths in new work outside the archive.

- **May read** the archive to learn what v1 did (behavior, copy, wire formats, hardware notes).
- **Must extract** facts into root `docs/`, new specs, or new code — standalone artifacts with no `poc-v1/...` links.
- **Do not** treat v1 architecture or as-built code as mandatory for a rewrite unless the owner confirms in an active spec.

For v1 build/flash commands and archive-local indexes, humans working inside the archive can use the archive’s own README and docs (not linked from new active docs).

## Product and engineering sources (active)

| Need | Start here |
|------|------------|
| Build, test, install, and future flash/evaluate mechanics | [`../config/host.defaults.ini`](../config/host.defaults.ini) · [`standards/project-configuration.md`](standards/project-configuration.md) |
| Desk kits, endpoints, users | [`../config/deployment/example.yaml`](../config/deployment/example.yaml) · [`standards/deployment-configuration.md`](standards/deployment-configuration.md) · `make config.deployment` |
| Device labs (host registry, flash/capture) | [`labs/README.md`](labs/README.md) · [`standards/labs-configuration.md`](standards/labs-configuration.md) · `make config.labs` · `make lab.help` |
| Experience spec authoring | [`product/experience-specification-authoring.md`](product/experience-specification-authoring.md) · skill: [`.cursor/skills/author-experience-specification/SKILL.md`](../.cursor/skills/author-experience-specification/SKILL.md) |
| Client/server contract authoring | [`product/communication-contract-authoring.md`](product/communication-contract-authoring.md) · skill: [`.cursor/skills/author-communication-contract/SKILL.md`](../.cursor/skills/author-communication-contract/SKILL.md) |
| Client coding standards | [`standards/client-application-coding-standards.md`](standards/client-application-coding-standards.md) |
| Client implementation review (read-only → fix → verify) | [`standards/client-code-review-agent.md`](standards/client-code-review-agent.md) · skill: [`.cursor/skills/client-code-review/SKILL.md`](../.cursor/skills/client-code-review/SKILL.md) |
| v1 evidence assessments | [`v1-assessments/`](v1-assessments/carousel-playback.md) (historical evidence, not normative architecture) |
| Device labs (v2) | [`labs/README.md`](labs/README.md) · [`labs/assistant-guide.md`](labs/assistant-guide.md) · [`standards/labs-configuration.md`](standards/labs-configuration.md) |
| Deep retrospective method | [`.cursor/skills/deep-implementation-retrospective/SKILL.md`](../.cursor/skills/deep-implementation-retrospective/SKILL.md) |

Codex/Cursor skill copies also live under [`.agents/skills/`](../.agents/skills/author-experience-specification/SKILL.md) (keep in sync with `.cursor/skills/`).

## Authority

- **Owner intent** — explicit decisions and approved experience specifications under `docs/product/`.
- **Portable standards** — `docs/standards/` and concept guides in `docs/v1-assessments/`.
- **v1 as-built** — read from the POC archive when needed; cite extracted content in active docs, not archive paths.

When sources conflict, state the conflict and required decision. Do not silently prefer the easiest implementation.

## Where to put new work

| Change type | Location |
|-------------|----------|
| New experience / behavioral contract | `docs/product/experiences/<slug>.md` (or path the owner names) |
| Client/server exchange, identity, retry, and recovery contract | `docs/product/communication-contracts/<scope>.md` |
| Product policy or authoring workflow | `docs/product/` |
| Portable client rules | `docs/standards/` |
| Product firmware | [`client/device/`](../client/device/) |
| Lab firmware / lab servers | [`client/labs/<slug>/`](../client/labs/) · [`server/labs/<slug>/`](../server/labs/) per [`labs/repository-layout.md`](labs/repository-layout.md) |
| Lab evidence reports | [`docs/labs/evidence/`](labs/evidence/README.md) |
| Other new implementation | Outside `poc-v1/` at repo root |

Do not update `poc-v1/` for forward product work.

## Shell and terminal

| OS | Shell | Rule |
|----|-------|------|
| Windows | PowerShell 7 (`pwsh`) | [`.cursor/rules/shell-conventions.mdc`](../.cursor/rules/shell-conventions.mdc) |
| macOS | zsh | Same rule |

Workspace profiles: [`.vscode/settings.json`](../.vscode/settings.json) (`defaultProfile` + `automationProfile` per OS).

If Agent terminal behavior diverges from the integrated terminal on Windows, enable **Cursor Settings → Chat → Inline Editing & Terminal → Legacy Terminal Tool**, then fully restart Cursor so automation profiles apply.
