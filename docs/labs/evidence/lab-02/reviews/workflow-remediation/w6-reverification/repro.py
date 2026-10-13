import json, tempfile
from pathlib import Path
import lab02_evidence as m

with tempfile.TemporaryDirectory() as td:
    r=m.EvidenceRun(Path(td),'event-bound',{},max_events=2)
    for n in range(8): r.event_sink({'kind':'tick','n':n})
    print('event bound',len(r._events),len(r.events_path.read_text().splitlines()),r._dropped_events)
with tempfile.TemporaryDirectory() as td:
    r=m.EvidenceRun(Path(td),'retry',{}); original=m._atomic_json
    def fail(*a,**kw): raise OSError('simulated disk full')
    m._atomic_json=fail
    try:
        try:r.finalize(workflow_status='FAIL',experiment_verdict='UNMEASURED',caller_completed=False,cleanup_succeeded=True)
        except OSError: print('first failure',r._final is not None,r.manifest_path.exists())
    finally: m._atomic_json=original
    x=r.finalize(workflow_status='PASS',experiment_verdict='PASS',caller_completed=True,cleanup_succeeded=True,terminal_reason='changed')
    rows=[json.loads(s) for s in r.events_path.read_text().splitlines()]
    print('retry',x['workflow_status'],x['experiment_verdict'],sum(x.get('kind')=='run_terminal' for x in rows),r.manifest_path.exists())
with tempfile.TemporaryDirectory() as td:
    r=m.EvidenceRun(Path(td),'artifact',{'artifacts':{'serial_log':'private/local.yaml','metrics':'unrelated.txt','device_log':'logs/device/labs/lab-02/a.txt'}})
    x=r.finalize(workflow_status='FAIL',experiment_verdict='UNMEASURED',caller_completed=False,cleanup_succeeded=True)
    print('arbitrary artifact refs',x['artifacts']['serial_log'],x['artifacts']['metrics'],x['artifacts']['device_log'])
with tempfile.TemporaryDirectory() as td:
    r=m.EvidenceRun(Path(td),'terminal-bound',{},max_events=1)
    r.event_sink({'kind':'too-large','data':'x'*20000})
    r.finalize(workflow_status='FAIL',experiment_verdict='UNMEASURED',caller_completed=False,cleanup_succeeded=True,terminal_reason='r'*20000)
    sizes=[len(line.encode()) for line in r.events_path.read_text().splitlines()]
    print('oversized terminal boundary',sizes,'max',r.max_event_bytes)
