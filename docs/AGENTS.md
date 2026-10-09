# Agent guide — Family Link (active repo)

**Documentation index:** [`README.md`](README.md). Read it before inventing product behavior.

## Repository layout

| Area | Role |
|------|------|
| [`docs/`](README.md) | Active product documentation, standards, v1 assessments, experience-spec workflow |
| [`../project.defaults.ini`](../project.defaults.ini) | Agent-readable repository commands, flows, and host defaults |
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
| Build, test, install, and future flash/evaluate mechanics | [`../project.defaults.ini`](../project.defaults.ini) · contract: [`standards/project-configuration.md`](standards/project-configuration.md) |
| Experience spec authoring | [`product/experience-specification-authoring.md`](product/experience-specification-authoring.md) · skill: [`.cursor/skills/author-experience-specification/SKILL.md`](../.cursor/skills/author-experience-specification/SKILL.md) |
| Client coding standards | [`standards/client-application-coding-standards.md`](standards/client-application-coding-standards.md) |
| v1 evidence assessments | [`v1-assessments/`](v1-assessments/carousel-playback.md) (historical evidence, not normative architecture) |
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
| Product policy or authoring workflow | `docs/product/` |
| Portable client rules | `docs/standards/` |
| New implementation | Outside `poc-v1/` (future app directories at repo root) |

Do not update `poc-v1/` for forward product work.

## Shell and terminal

| OS | Shell | Rule |
|----|-------|------|
| Windows | PowerShell 7 (`pwsh`) | [`.cursor/rules/shell-conventions.mdc`](../.cursor/rules/shell-conventions.mdc) |
| macOS | zsh | Same rule |

Workspace profiles: [`.vscode/settings.json`](../.vscode/settings.json) (`defaultProfile` + `automationProfile` per OS).

If Agent terminal behavior diverges from the integrated terminal on Windows, enable **Cursor Settings → Chat → Inline Editing & Terminal → Legacy Terminal Tool**, then fully restart Cursor so automation profiles apply.
