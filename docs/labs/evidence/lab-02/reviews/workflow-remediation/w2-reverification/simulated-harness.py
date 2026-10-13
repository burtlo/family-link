import contextlib, io, os, sys, tempfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path.cwd() / 'scripts'))
import lab_host
from deployment_config import Deployment, DeploymentMeta, Endpoint, Kit
from device_targets import Port

ka = Kit('A', 'endpoint-a', 'profile-a', 'SERIAL-A')
kb = Kit('B', 'endpoint-b', 'profile-b', 'SERIAL-B')
dep = Deployment(Path('local.yaml'), 'local', DeploymentMeta('test','test'), (), (Endpoint('endpoint-a','A','token-a','hangout-a'),Endpoint('endpoint-b','B','token-b','hangout-b')), (ka,kb), {'desk':'endpoint-a'})
class Config:
    def validate(self): pass
    def text(self, key, required=False): return 'esp32s3' if key == 'idf.target' else ''
    def integer(self, key, default, minimum): return default
    def path_value(self, key): return Path('/tmp/lab02-w2-reverify/logs')
config = Config()

def invoke(argv, attached, expect, *, env=None):
    events=[]
    def build(*a, **kw): events.append(('build',kw.get('kit'),kw.get('port'),kw.get('target'))); return Path('/tmp/fake-build')
    def capture(*a, **kw): events.append(('capture',kw.get('kit'),kw.get('port'),kw.get('target'))); return Path('/tmp/fake-log')
    out=io.StringIO(); err=io.StringIO()
    with patch.object(lab_host.sys,'argv',['lab_host.py',*argv]), patch.object(lab_host,'ProjectConfig',return_value=config), patch.object(lab_host,'load_active_deployment',return_value=dep), patch.object(lab_host,'ports',return_value=attached), patch.object(lab_host,'ensure_lab_built',side_effect=build), patch.object(lab_host,'capture_lab',side_effect=capture), patch.dict(os.environ, {'KIT':'','PORT':'','FLASH':'0', **(env or {})}, clear=False), contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        rc=lab_host.main()
    assert rc == expect, (argv,rc,out.getvalue(),err.getvalue())
    return events,err.getvalue()

# No KIT: actual resolver must pick A and same frozen target reaches both build and capture.
events,err=invoke(['run','lab02'],[Port('/dev/a','serial-a')],0)
assert [x[0] for x in events] == ['build','capture'], events
assert events[0][1:3] == ('A','/dev/a') and events[1][1:3] == ('A','/dev/a'), events
assert events[0][3] is events[1][3] and events[0][3].serial == 'serial-a', events
# Alias KIT and matching PORT resolves the same registered identity.
events,err=invoke(['run','lab02','--kit','desk','--port','/dev/a'],[Port('/dev/a','SERIAL-A')],0)
assert events[0][1:3] == ('A','/dev/a') and events[1][1:3] == ('A','/dev/a'), events
# KIT=A + device B rejected before build or capture.
events,err=invoke(['run','lab02','--kit','A','--port','/dev/b'],[Port('/dev/a','SERIAL-A'),Port('/dev/b','SERIAL-B')],1)
assert events == [], events
assert 'does not match' in err, err
# Direct flash API rejects KIT=A + device B before invoking esptool.
flash_events=[]; out=io.StringIO(); err=io.StringIO()
with patch.object(lab_host.sys,'argv',['lab_host.py','flash','lab02','--kit','A','--port','/dev/b']), patch.object(lab_host,'ProjectConfig',return_value=config), patch.object(lab_host,'load_active_deployment',return_value=dep), patch.object(lab_host,'ports',return_value=[Port('/dev/a','SERIAL-A'),Port('/dev/b','SERIAL-B')]), patch.object(lab_host,'flash_lab_build',side_effect=lambda *a: flash_events.append(a)), patch.dict(os.environ, {'KIT':'','PORT':''}, clear=False), contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
    rc=lab_host.main()
assert rc == 1 and flash_events == [] and 'does not match' in err.getvalue(), (rc,flash_events,err.getvalue())
# No KIT with zero or two registered connected kits fails before build/capture.
for attached in ([],[Port('/dev/a','SERIAL-A'),Port('/dev/b','SERIAL-B')]):
    events,err=invoke(['run','lab02'],attached,1)
    assert events == [], events
# Public build action resolves alias via build implementation and performs no USB enumeration.
with tempfile.TemporaryDirectory() as tmp:
    events=[]
    with patch.object(lab_host.sys,'argv',['lab_host.py','build','lab02','--variant','phaseb','--kit','desk']), patch.object(lab_host,'ProjectConfig',return_value=config), patch.object(lab_host,'load_active_deployment',return_value=dep), patch.object(lab_host,'ports',side_effect=AssertionError('build enumerated USB')), patch.object(lab_host,'ROOT',Path(tmp)), patch.object(lab_host,'idf_environment',return_value=({},['idf.py'])), patch.object(lab_host,'write_deployment_kit_sdkconfig',return_value=None), patch.object(lab_host,'run',side_effect=lambda *a,**kw: events.append((a,kw))):
        rc=lab_host.main()
    assert rc == 0 and len(events) == 1, (rc,events)
print('PASS: no-KIT single A selects/reuses A through actual run CLI; nickname + matching PORT works; KIT A + port B and 0/many auto targets and direct invalid flash stop before writes; public Phase B build by nickname does not enumerate USB')

# Exercise the real capture_lab resolver and alias canonicalization with simulated USB and capture.
import tempfile
from lab_host import capture_lab
with tempfile.TemporaryDirectory() as tmp, patch.object(lab_host,'load_active_deployment',return_value=dep), patch.object(lab_host,'ports',return_value=[Port('/dev/a','SERIAL-A')]), patch.object(lab_host,'print_run_start'), patch.object(lab_host,'print_operator_help'), patch.object(lab_host,'capture_serial',side_effect=lambda _port,path,_seconds: path.parent.mkdir(parents=True,exist_ok=True)), patch.object(lab_host,'parse_serial_file'):
    target = lab_host.resolve_run_target(config, 'A', '/dev/a')
    for supplied in (None, target):
        capture_lab(config,'lab02',kit='desk',port='/dev/a',variant='phaseb',seconds=1,flash=False,run_id='alias-api',emit_operator_help=False,target=supplied)
# Supplied target must still reject a different KIT before flash or serial capture.
with patch.object(lab_host,'load_active_deployment',return_value=dep), patch.object(lab_host,'ports',return_value=[Port('/dev/a','SERIAL-A')]):
    target = lab_host.resolve_run_target(config, 'A', '/dev/a')
with patch.object(lab_host,'print_run_start'), patch.object(lab_host,'print_operator_help'), patch.object(lab_host,'revalidate_run_target') as revalidate, patch.object(lab_host,'flash_lab_build') as flash, patch.object(lab_host,'capture_serial') as serial_capture:
    try:
        capture_lab(config,'lab02',kit='B',port='/dev/a',variant='phaseb',seconds=1,flash=True,run_id='mismatch',emit_operator_help=False,target=target)
    except lab_host.ProjectConfigError as exc:
        assert 'does not match requested KIT' in str(exc), exc
    else:
        raise AssertionError('capture_lab accepted mismatched KIT')
    revalidate.assert_not_called(); flash.assert_not_called(); serial_capture.assert_not_called()
print('PASS: actual capture_lab accepts KIT nickname with internally resolved and supplied frozen target; mismatched KIT fails before revalidation/flash/capture')
