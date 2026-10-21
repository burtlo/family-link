#!/usr/bin/env python3
"""Strict parser and phase verdict evaluator for LAB-12 traces."""
from __future__ import annotations
import argparse, json, re
from pathlib import Path

DECIDED={"visual":["P-preview","P-modal","P-return"],"shared":["P-save-fail","P-conflict","P-lost","P-invalid","P-hint"]}
EXPLORATORY=["P-save-policy","P-offline","P-message-color","P-layout"]
REQUIRED={"P-preview":("cancelled",{"saved":0}),"P-modal":("success",{"overlay_widgets":0,"focus_id":"card-2"}),"P-return":("success",{"inbox_root":1,"focus_id":"card-2"}),
"P-save-fail":("failure",{"presented_saved":0}),"P-conflict":("revision_conflict",{"first_success":1,"first_body_unchanged":1}),
"P-lost":("success",{"replayed_same_operation_id":1,"replay_count":1,"side_effect_count":1}),"P-invalid":("invalid",{"profile_unchanged":1,"fallback_avatar":1}),"P-hint":("success",{"stroke_color_changed":0,"inbox_rebuilds":0})}
BLOCKED=re.compile(r'(?i)(?:"(?:pin|payload_b64|media_payload|image_data|audio_data)"\s*:|Bearer\s+\S+|password\s*[=:]\s*[^\s,;]+)')

class TraceError(ValueError): pass
def parse_text(text: str, run_id: str, expected_phase: str, expected_variant: str, *, post_reset: bool=True) -> dict:
    if expected_phase not in DECIDED or not post_reset: raise TraceError("phase or reset-gate invalid")
    if BLOCKED.search(text): raise TraceError("raw secret or media field in trace")
    ready=[]; done=[]; rows=[]
    for line in text.splitlines():
        if line.startswith("LAB12_READY "): ready.append(line)
        elif line.startswith("LAB12_HARNESS_DONE "): done.append(line)
        elif "LAB12_TRACE " in line:
            raw=line.split("LAB12_TRACE ",1)[1]
            if len(raw)>4096: raise TraceError("oversize trace row")
            try: row=json.loads(raw)
            except Exception as exc: raise TraceError("malformed trace JSON") from exc
            if not isinstance(row,dict): raise TraceError("trace row must be object")
            rows.append(row)
    if len(ready)!=1: raise TraceError("missing or duplicate post-reset ready marker")
    expected_server="lab12-profile/v1" if expected_phase=="shared" else "none"
    marker=dict(token.split("=",1) for token in ready[0].split()[1:] if "=" in token)
    if set(marker)!={"build","phase","server","layout"} or marker.get("phase")!=expected_phase or marker.get("server")!=expected_server or not marker.get("build") or marker.get("layout")!="split-pane":
        raise TraceError("ready marker phase/server mismatch")
    harness_complete=bool(done) and len(done)==1 and done[0]==f"LAB12_HARNESS_DONE status=complete phase={expected_phase}"
    if expected_variant.startswith("scripted") and not harness_complete: raise TraceError("scripted harness marker missing or mismatched")
    expected_ids=DECIDED[expected_phase]
    by_scenario={}
    opids=set()
    for row in rows:
        scenario=row.get("scenario"); op=row.get("operation_id")
        if not isinstance(scenario,str) or not isinstance(op,int) or op<=0 or op in opids: raise TraceError("missing or duplicate operation identity")
        opids.add(op)
        if row.get("terminal_count")!=1: raise TraceError("terminal count must be one")
        if scenario in by_scenario: raise TraceError("duplicate scenario")
        if scenario not in expected_ids and scenario not in EXPLORATORY: raise TraceError("unknown scenario")
        by_scenario[scenario]=row
    for sid in expected_ids:
        row=by_scenario.get(sid)
        if row is None: continue
        terminal,fields=REQUIRED[sid]
        if row.get("terminal")!=terminal or any(row.get(k)!=v for k,v in fields.items()):
            return _metrics(run_id,expected_phase,expected_variant,by_scenario,"FAIL",harness_complete,"decided scenario violation")
        if sid=="P-return" and (not isinstance(row.get("update_level"),int) or row["update_level"]>1):
            return _metrics(run_id,expected_phase,expected_variant,by_scenario,"FAIL",harness_complete,"return update level exceeds one")
        if sid=="P-conflict" and row.get("revision")!=8:
            return _metrics(run_id,expected_phase,expected_variant,by_scenario,"FAIL",harness_complete,"conflict revision mismatch")
        if sid=="P-preview" and row.get("success_copy",0)!=0:
            return _metrics(run_id,expected_phase,expected_variant,by_scenario,"FAIL",harness_complete,"cancel showed success copy")
    if "P-save-policy" in by_scenario and by_scenario["P-save-policy"].get("reject_preview")!="rollback": raise TraceError("save policy does not match frozen fixture")
    if "P-offline" in by_scenario and (by_scenario["P-offline"].get("work")!="retained" or by_scenario["P-offline"].get("presented_saved")!=0): raise TraceError("offline observation differs from frozen fixture")
    if "P-message-color" in by_scenario and by_scenario["P-message-color"].get("values")!=["follow-profile","frozen-on-message"]: raise TraceError("message-color observations differ from fixture")
    if "P-layout" in by_scenario and (by_scenario["P-layout"].get("layout")!="split-pane" or by_scenario["P-layout"].get("upper")!=[320,80] or by_scenario["P-layout"].get("lower")!=[320,160]): raise TraceError("layout observation differs from frozen geometry fixture")
    for sid in EXPLORATORY:
        if sid in by_scenario and by_scenario[sid].get("class")!="exploratory": raise TraceError("exploratory classification missing")
    present=all(sid in by_scenario for sid in expected_ids)
    verdict="PASS" if present and harness_complete else ("INCOMPLETE" if any(sid in by_scenario for sid in expected_ids) else "UNMEASURED")
    return _metrics(run_id,expected_phase,expected_variant,by_scenario,verdict,harness_complete,None if present else "missing decided scenario")

def _metrics(run_id,phase,variant,rows,verdict,harness,reason):
    scenarios={}
    for key,row in rows.items():
        if row.get("class")=="exploratory": scenarios[key]={"class":"exploratory","observation":{"terminal":row.get("terminal"),**{k:v for k,v in row.items() if k not in {"scenario","class","terminal","operation_id","terminal_count"}}}}
        else: scenarios[key]={"class":"decided","terminal":row.get("terminal"),"terminal_count":row.get("terminal_count"),"fields":{k:v for k,v in row.items() if k not in {"scenario","class","terminal","operation_id","terminal_count"}}}
    return {"document":"family-link.lab-metrics/v2","run_id":run_id,"lab_metrics":{"experiment_phase":phase,"variant":variant,"experiment_inputs":DECIDED[phase],"experiment_verdict":verdict,"harness_complete":harness,"missing_reason":reason,"scenarios":scenarios,"performance":{"status":"UNMEASURED","provenance":"unmeasured/blocked"}}}

def parse_file(path, output_path=None, expected_run_id="host-test", expected_phase="visual", expected_variant="scripted", **kwargs):
    result=parse_text(Path(path).read_text(encoding="utf-8"),expected_run_id,expected_phase,expected_variant,post_reset=kwargs.get("post_reset",True))
    if output_path: Path(output_path).write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    return result
def main():
    p=argparse.ArgumentParser(); p.add_argument("serial"); p.add_argument("--run-id",default="host-test"); p.add_argument("--phase",choices=DECIDED,default="visual"); p.add_argument("--variant",default="scripted"); p.add_argument("--output"); a=p.parse_args()
    print(json.dumps(parse_file(a.serial,a.output,a.run_id,a.phase,a.variant),indent=2))
if __name__=="__main__": main()
