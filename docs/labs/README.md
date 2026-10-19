# Device and experience labs (v2)

Status: **active lab registry and evidence process** — registered projects and proof status are listed below; planned labs remain projected until implemented.
Purpose: measure v2 carousel, media, login, and journey behavior on hardware **before** folding results into [`client/device`](../../client/device/) (product firmware).

## Start here

| Audience | Document |
|----------|----------|
| Anyone scoping or running a lab | [Assistant guide](assistant-guide.md) |
| LAB-02 remediation | [Workflow contract and durable remediation plan](lab-02-workflow-remediation-plan.md) · [Carousel application remediation plan](lab-02-carousel-remediation-plan.md) · [review record](evidence/lab-02/reviews/2026-10-12-workflow.md) |
| Where code, servers, builds, and evidence live | [Repository layout](repository-layout.md) |
| Reusable host-runner proposal from the LAB-02 review | [Reusable lab runner pattern](reusable-lab-runner.md) |
| **LAB-02** file tree, phases, verification scenarios | [LAB-02 implementation outline](lab-02-implementation-outline.md) |
| Run and metrics JSON shapes | [shared manifest v2](schemas/run-manifest.v2.md) · [metrics v2](schemas/metrics.v2.md) · [legacy LAB-02 manifest v1](schemas/run-manifest.v1.md) |
| Inbox carousel, card reuse, audio/sketch (`LAB-01`–`LAB-05`) | [Inbox carousel playback evaluation](inbox-carousel-playback-evaluation.md) |
| Login, session, capture/send, composition (`LAB-06`–`LAB-13`) | [V2 experience demo roadmap](v2-experience-demo-roadmap.md) |

Read [`../AGENTS.md`](../AGENTS.md) before changing product behavior or the v1 archive. Labs **do not** approve OPEN contract or experience decisions; they produce evidence and recommended decisions.

## Lab ID registry

Use these IDs in manifests, traces, and evidence paths. Folder names are **kebab-case** under [`client/labs/`](repository-layout.md#firmware) and [`server/labs/`](repository-layout.md#servers).

| ID | Folder slug (firmware) | Folder slug (server, if any) | Plan |
|----|------------------------|------------------------------|------|
| `LAB-01` | `lab-01-x02-baseline` | `lab-01-x02-baseline` | [Inbox plan § LAB-01](inbox-carousel-playback-evaluation.md#lab-01--x02-integrated-baseline) |
| `LAB-02` | `lab-02-bounded-inbox` | `lab-02-paging` (managed Phase B default) | [Inbox plan § LAB-02](inbox-carousel-playback-evaluation.md#lab-02--bounded-inbox-and-card-recycling) |
| `LAB-03` | `lab-03-media-playback` | `lab-03-media` | [Inbox plan § LAB-03](inbox-carousel-playback-evaluation.md#lab-03--audiosketch-switching-and-pause) |
| `LAB-04` | `lab-04-sketch-format` | optional shared with `LAB-03` | [Inbox plan § LAB-04](inbox-carousel-playback-evaluation.md#lab-04--color-sketch-format-and-preview-strategies) |
| `LAB-05` | `lab-05-transition-peak` | `none` | [Inbox plan § LAB-05](inbox-carousel-playback-evaluation.md#lab-05--transition-and-simultaneous-resource-peak) |
| `LAB-06` | `lab-06-stage-shell` | `none` | [Roadmap § LAB-06](v2-experience-demo-roadmap.md#lab-06--stage-shell-and-panel-lifecycle) |
| `LAB-07` | `lab-07-readiness-roster` | `lab-07-readiness-roster` | [Roadmap § LAB-07](v2-experience-demo-roadmap.md#lab-07--readiness-roster-mute-and-endpoint-health) |
| `LAB-08` | `lab-08-pin-cooldown` | `lab-08-pin-cooldown` | [Roadmap § LAB-08](v2-experience-demo-roadmap.md#lab-08--pin-operation-retry-and-cooldown) |
| `LAB-09` | `lab-09-session-isolation` | `lab-09-session-isolation` | [Roadmap § LAB-09](v2-experience-demo-roadmap.md#lab-09--signed-in-authority-and-late-event-isolation) |
| `LAB-10` | `lab-10-record-canvas` | optional media fixture | [Roadmap § LAB-10](v2-experience-demo-roadmap.md#lab-10--record-canvas-and-media-budget) |
| `LAB-11` | `lab-11-send-receipt` | `lab-11-send-receipt` | [Roadmap § LAB-11](v2-experience-demo-roadmap.md#lab-11--send-receipt-and-recovery) |
| `LAB-12` | `lab-12-profile-settings` | `lab-12-profile-settings` | [Roadmap § LAB-12](v2-experience-demo-roadmap.md#lab-12--profile-and-settings-propagation) |
| `LAB-13` | `lab-13-full-journey` | `lab-13-full-journey` | [Roadmap § LAB-13](v2-experience-demo-roadmap.md#lab-13--composed-v2-journey) |

`LAB-02`, `LAB-03`, and `LAB-05` have registered firmware projects. LAB-03 adds paired fixed media fixtures and a scripted sustained run; its criteria are provisional and its acoustic/LCD evidence remains incomplete. See [LAB-03 report](evidence/lab-03/REPORT-LAB-03-v2.md). The current LAB-05 manual and scripted integration proofs pass the workflow lifecycle with service and scenario stages `not_configured`; experiment verdicts remain `UNMEASURED`. The scripted capture lacks its `LAB05_HARNESS_DONE` marker, and these runs do not measure performance. See [evidence index](evidence/README.md#shared-run-evidence-examples). Other rows below describe planned experiments, not registered runnable labs.

**Run on hardware:** `make lab.lab02` runs managed HTTP with manual free traversal; `make lab.lab02.scripted` runs its harness. `make lab.lab05` and `make lab.lab05.scripted` exercise the no-server generic lifecycle. These target commands require the corresponding registered kit, serial access, and toolchain. Step-by-step for assistants: [assistant guide § Host commands and registering a lab](assistant-guide.md#host-commands-make-lab).

## Evidence

Published run artifacts and decision reports live under [`evidence/`](evidence/README.md), not inside firmware source trees. Each report links to a **lab version** string, for example `LAB-02/v1 = firmware <build-id> + server <revision> + fixture inbox-summary/v1`.

## Related standards

- [Labs configuration](../standards/labs-configuration.md) — host registry YAML, schema, `make lab.*`, `make config.labs`
- [Client application coding standards](../standards/client-application-coding-standards.md) — ownership, verification ladder, bounded work
- [Device build and flash](../standards/device-build-flash.md) — product bootstrap via `make build` (labs use separate build dirs; see [repository layout](repository-layout.md#build-and-host-configuration))
- [Project configuration](../standards/project-configuration.md) — host INI, inspection snapshots, optional `[command.lab.*]` catalog entries
