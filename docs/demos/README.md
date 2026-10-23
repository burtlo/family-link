# Island demo execution catalog (active docs)

Status: **navigation and proof summaries** for legacy v1 POC **island** firmware demos that are not registered as v2 **LAB-** projects.

The runnable binaries and servers live in the **v1 POC archive** (frozen tree). These documents capture **what was proven on hardware**, **how to run**, and **engineering decisions** in the active repo so v2 work does not depend on chat history.

| Doc | Demo ID | Role |
|-----|---------|------|
| [Opus chunk end-to-end (h31)](opus-chunk-end-to-end-h31.md) | **h31** | Only full **record → Opus chunks → server Ogg → playback** path in the archive; owner-validated Oct 2026 |

For the full island ID list (h / p / x), see the [demo explorer index](../product/device-ui/assessments/DEMO-EXPLORER-INDEX.md). For v2 measurement labs (`LAB-01`–`LAB-16`), see [labs/README.md](../labs/README.md).

**Flash convention (archive):** `make flash DEMO=<id> PROFILE=sunset WHO=<box>` from the v1 POC firmware Makefile on the host configured in project settings. **h31** uses demo id `h31` (Opus chunks).
