# W8 verifier status clarification

Fresh independent Luna `/root/w8_verify` supplied this evidence-only correction after its initial report. The earlier report is preserved.

The scripted `workflow_status=FAIL` is justified, not a confirmed status defect. The required scripted harness traversal aborted at `inbox_not_ready`, before expected steps and scenario completion. The plan allows failed/incomplete scenario stages to fail the workflow; semantic failure may coexist with PASS only when required host stages actually complete. The parser rejects the early terminal format safely. The detailed reason is retained in serial.txt; extracting it into the manifest is a possible telemetry improvement, not a demonstrated normative defect. Keep FAIL/UNMEASURED and separate app readiness evidence. No source, maintained documents, or hardware were changed by the verifier.
