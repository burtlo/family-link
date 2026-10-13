import tempfile
from pathlib import Path
import lab02_evidence as m
with tempfile.TemporaryDirectory() as td:
    r = m.EvidenceRun(Path(td), 'bounded', {}, max_events=2)
    for n in range(8): r.event_sink({'kind':'tick','n':n})
    print('bounded_memory_count', len(r._events), 'configured', r.max_events)
    print('bounded_file_count', len(r.events_path.read_text().splitlines()))
    print('bounded_memory', [x.get('kind') for x in r._events])
with tempfile.TemporaryDirectory() as td:
    r = m.EvidenceRun(Path(td), 'retry', {})
    original = m._atomic_json
    def fail(*args, **kwargs): raise OSError('simulated disk full')
    m._atomic_json = fail
    try:
        try: r.finalize(workflow_status='FAIL', experiment_verdict='UNMEASURED', caller_completed=False, cleanup_succeeded=True)
        except OSError as e: print('first_finalize_error', repr(str(e)), 'cached', r._final is not None, 'manifest_exists', r.manifest_path.exists())
    finally: m._atomic_json = original
    r.finalize(workflow_status='FAIL', experiment_verdict='UNMEASURED', caller_completed=False, cleanup_succeeded=True)
    print('retry_manifest_exists', r.manifest_path.exists(), 'events_terminal_count', r.events_path.read_text().count('run_terminal'))
with tempfile.TemporaryDirectory() as td:
    r = m.EvidenceRun(Path(td), 'artifact', {'artifacts': {'serial_log':'private/local.yaml','metrics':'unrelated.txt','device_log':'logs/device/labs/lab-02/a.txt'}})
    out = r.finalize(workflow_status='FAIL', experiment_verdict='UNMEASURED', caller_completed=False, cleanup_succeeded=True)
    print('artifact_refs', out['artifacts'])
