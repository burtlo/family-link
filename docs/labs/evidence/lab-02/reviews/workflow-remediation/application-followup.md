# LAB02APP-F001 — inbox unavailable during W8

**Disposition:** Deferred application finding/observation; no application repair authorized by the workflow assignment. **Source boundary:** main HEAD `0b02b30086f1c8c8e985458912646b0d473b967b` plus the W1–W7 source hashes recorded in [W8 verification](w8-verification/source-and-artifact-hashes.txt).

**Trigger:** Run the default managed HTTP fixture-64 nominal scripted and manual commands on the uniquely selected registered kit. Both images were freshly built/flashed and matched the per-run readiness identity.

**Observed effect:** Scripted traversal aborted with `HARNESS_DONE ... status=inbox_not_ready`. Manual capture completed, but the owner saw “inbox unavailable” above “loading inbox” and confirmed that swiping left the content unchanged. Serial contains Wi-Fi timeout/failure followed by later station addressing and `fetch_fail`. These facts do not establish the cause of unavailable paging or a rendering/state defect.

**Evidence:** [Scripted manifest](../../runs/20261012-w8-scripted-01/run-manifest.json), [manual manifest](../../runs/20261012-w8-manual-01/run-manifest.json), and serial artifacts referenced by those manifests. Physical workflow and owner touch evidence are L3; the owner statement is independent of serial-derived metrics. [Independent review](w8-verification/report.txt) and [status clarification](w8-verification/status-clarification.md).

**Authority/boundary:** O-07 defers application functionality fixes. Manual free traversal/loading cards are O-03; observed loading is not evidence that push/removal identity semantics passed or failed. No product performance threshold or root cause is inferred. Nominal experiment verdict remains UNMEASURED; mutation scenarios were not physically measured.

**Follow-up:** Conduct a separate application review of readiness, Wi-Fi/HTTP completion, retry behavior and inbox availability with these traces before selecting a fix. Preserve the prior stack-overflow/blank-viewport diagnostics as separate historical observations. No repair was performed here.
