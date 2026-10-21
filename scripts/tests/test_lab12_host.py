from __future__ import annotations
import importlib.util, json, sys, threading, unittest
import time
from types import SimpleNamespace
from unittest.mock import patch
from http.server import ThreadingHTTPServer
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
PROJECT=ROOT/'client/labs/lab-12-profile-settings'
SERVER_FILE=ROOT/'server/labs/lab-12-profile-settings/server.py'
PARSER_FILE=PROJECT/'tools'/'parse_lab_trace.py'
PARSER_SPEC=importlib.util.spec_from_file_location('lab12_owned_trace_parser',PARSER_FILE)
PARSER=importlib.util.module_from_spec(PARSER_SPEC)
sys.modules[PARSER_SPEC.name]=PARSER
PARSER_SPEC.loader.exec_module(PARSER)
DECIDED, EXPLORATORY, TraceError, parse_text = PARSER.DECIDED, PARSER.EXPLORATORY, PARSER.TraceError, PARSER.parse_text

def load_server():
    spec=importlib.util.spec_from_file_location('lab12_fixture_server_test',SERVER_FILE)
    mod=importlib.util.module_from_spec(spec); sys.modules[spec.name]=mod; spec.loader.exec_module(mod); return mod

class Lab12ParserTests(unittest.TestCase):
    def test_lab12_manifest_and_parser_metrics_contract(self):
        manifest=json.loads((PROJECT/'manifest.lab.json').read_text())
        for key in ('document','lab_id','firmware_path','server_path','fixture_schema','fixture_revision','protocol','trace_schema'):
            self.assertIn(key,manifest)
        result=parse_text(self.rows('visual'),'host-test','visual','scripted')
        self.assertEqual(result['document'],'family-link.lab-metrics/v2')
        self.assertEqual(result['run_id'],'host-test')

    def rows(self,phase):
        ids=DECIDED[phase]; content=[]
        for i,sid in enumerate(ids,1):
            vals={
                'P-preview':{'terminal':'cancelled','saved':0,'success_copy':0},
                'P-modal':{'terminal':'success','focus_id':'card-2','overlay_widgets':0},
                'P-return':{'terminal':'success','inbox_root':1,'focus_id':'card-2','update_level':1},
                'P-save-fail':{'terminal':'failure','presented_saved':0},
                'P-conflict':{'terminal':'revision_conflict','first_success':1,'revision':8,'first_body_unchanged':1},
                'P-lost':{'terminal':'success','replayed_same_operation_id':1,'replay_count':1,'side_effect_count':1},
                'P-invalid':{'terminal':'invalid','profile_unchanged':1,'fallback_avatar':1},
                'P-hint':{'terminal':'success','stroke_color_changed':0,'inbox_rebuilds':0},
            }[sid]
            content.append({'scenario':sid,'operation_id':i,'terminal_count':1,**vals})
        for n,sid in enumerate(EXPLORATORY,20):
            if sid=='P-layout': obs={'layout':'split-pane','upper':[320,80],'lower':[320,160]}
            elif sid=='P-save-policy': obs={'reject_preview':'rollback'}
            elif sid=='P-offline': obs={'work':'retained','presented_saved':0}
            else: obs={'values':['follow-profile','frozen-on-message']}
            content.append({'scenario':sid,'class':'exploratory','operation_id':n,'terminal':'observed','terminal_count':1,**obs})
        marker='lab12-profile/v1' if phase=='shared' else 'none'
        return '\n'.join([f'LAB12_READY build=test phase={phase} server={marker} layout=split-pane',*['LAB12_TRACE '+json.dumps(r) for r in content],f'LAB12_HARNESS_DONE status=complete phase={phase}'])
    def test_lab12_exact_phase_input_sets_and_exploratory(self):
        for phase in ('visual','shared'):
            result=parse_text(self.rows(phase),'host-test',phase,'scripted' if phase=='visual' else 'scripted-shared')
            metrics=result['lab_metrics']; self.assertEqual(metrics['experiment_inputs'],DECIDED[phase]); self.assertEqual(metrics['experiment_verdict'],'PASS')
            for sid in EXPLORATORY: self.assertEqual(metrics['scenarios'][sid]['class'],'exploratory')
    def test_lab12_decided_failure_not_exploratory_failure(self):
        malformed=self.rows('visual').replace('"saved": 0','"saved": 1')
        self.assertEqual(parse_text(malformed,'host-test','visual','scripted')['lab_metrics']['experiment_verdict'],'FAIL')
    def test_lab12_marker_and_secret_rejection(self):
        with self.assertRaises(TraceError): parse_text(self.rows('visual').replace('phase=visual','phase=shared',1),'host-test','visual','scripted')
        with self.assertRaises(TraceError): parse_text(self.rows('visual')+'\npassword=hunter2','host-test','visual','scripted')
    def test_lab12_missing_decided_sample_incomplete(self):
        text='\n'.join(line for line in self.rows('visual').splitlines() if 'P-modal' not in line)
        self.assertEqual(parse_text(text,'host-test','visual','scripted')['lab_metrics']['experiment_verdict'],'INCOMPLETE')

class Lab12ServerTests(unittest.TestCase):
    def setUp(self):
        self.server=load_server(); self.server.reset_state(); self.http=ThreadingHTTPServer(('127.0.0.1',0),self.server.Handler); self.http.run_id='host-test'
        self.thread=threading.Thread(target=self.http.serve_forever,daemon=True); self.thread.start(); self.base=f'http://127.0.0.1:{self.http.server_port}'
    def tearDown(self): self.http.shutdown(); self.http.server_close(); self.thread.join(2)
    def get(self,path):
        with urlopen(self.base+path,timeout=2) as r:return json.load(r)
    def patch(self,endpoint,body):
        req=Request(self.base+'/profile/'+endpoint,data=json.dumps(body).encode(),headers={'Content-Type':'application/json'},method='PATCH')
        try:
            with urlopen(req,timeout=2) as r:return r.status,json.load(r)
        except HTTPError as e:return e.code,json.load(e)
    def test_lab12_health_identity_and_fixture_hash(self):
        health=self.get('/health'); self.assertEqual(health['run_id'],'host-test'); self.assertEqual(health['fixture_sha256'],self.server.fixture_hash())
    def test_lab12_conflict_preserves_first_endpoint_body(self):
        original=self.get('/profile/endpoint-a')
        code,first=self.patch('endpoint-a',{'operation_id':'op-a','expected_revision':7,'changes':{'profile_accent':'#5AA0E8'}})
        self.assertEqual(code,200); self.assertEqual((first['user_id'],first['revision'],first['profile_accent']),('synthetic-user',8,'#5AA0E8'))
        # Matching requested values in canonical state do not acknowledge a new
        # operation whose expected revision is stale.
        second_code,second=self.patch('endpoint-b',{'operation_id':'op-b','expected_revision':7,'changes':{'profile_accent':'#5AA0E8'}})
        self.assertEqual(second_code,409); self.assertEqual(second['error'],'revision_conflict')
        current=self.get('/profile/endpoint-a'); self.assertEqual((current['revision'],current['avatar_id'],current['profile_accent']),(first['revision'],first['avatar_id'],first['profile_accent'])); self.assertEqual(first['profile_accent'],'#5AA0E8')
    def test_lab12_lost_response_replay_has_one_side_effect(self):
        req={'operation_id':'same-op','expected_revision':7,'changes':{'profile_accent':'#67C587'}}
        _,first=self.patch('endpoint-a',req); _,replay=self.patch('endpoint-a',req)
        self.assertEqual(first,replay); self.assertEqual(self.server.COUNTS['side_effects'],1)
        conflict,_=self.patch('endpoint-a',{**req,'changes':{'profile_accent':'#D875A8'}}); self.assertEqual(conflict,409)
    def test_lab12_delayed_competing_updates_have_one_winner(self):
        results={}
        a={'operation_id':'delayed-a','expected_revision':7,'fixture_delay_ms':80,'changes':{'profile_accent':'#5AA0E8'}}
        b={'operation_id':'fast-b','expected_revision':7,'changes':{'avatar_id':'avatar-02'}}
        def run(name,endpoint,body): results[name]=self.patch(endpoint,body)
        ta=threading.Thread(target=run,args=('a','endpoint-a',a));tb=threading.Thread(target=run,args=('b','endpoint-b',b));ta.start();time.sleep(.01);tb.start();ta.join(2);tb.join(2)
        self.assertEqual(results['b'][0],200);self.assertEqual(results['a'][0],409)
        current=self.get('/profile/endpoint-a');self.assertEqual(current['revision'],8);self.assertEqual(current['avatar_id'],'avatar-02')
    def test_lab12_invalid_fixture_color_does_not_write(self):
        code,response=self.patch('endpoint-a',{'operation_id':'invalid','expected_revision':7,'changes':{'profile_accent':'#000000'}})
        self.assertEqual((code,response['error']),(422,'invalid')); self.assertEqual(self.get('/profile/endpoint-a')['revision'],7)

class Lab12AdapterTests(unittest.TestCase):
    def test_lab12_help_does_not_advertise_backend_and_states_fixture_boundary(self):
        from lab_registry import get_lab, run_mode_hint_lines
        hints="\n".join(run_mode_hint_lines(get_lab('lab12')))
        self.assertIn('BACKEND is unsupported',hints)
        self.assertIn('shared variants own the LAB-12 fixture on port 8771',hints)
        self.assertNotIn('local comparison',hints)

    def test_lab12_shared_build_defaults_link_transport_without_starting_fixture(self):
        defaults=(PROJECT/'sdkconfig.defaults.shared').read_text(encoding='utf-8')
        scripted_shared=(PROJECT/'sdkconfig.defaults.scripted-shared').read_text(encoding='utf-8')
        base=(PROJECT/'sdkconfig.defaults').read_text(encoding='utf-8')
        partitions=(PROJECT/'partitions.csv').read_text(encoding='utf-8')
        self.assertIn('CONFIG_LAB12_WIFI_SSID="lab12-build-only"',defaults)
        self.assertIn('CONFIG_LAB12_WIFI_PASSWORD="lab12-build-only"',defaults)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM=y',base)
        self.assertIn('factory, app, factory, , 0x1E0000,',partitions)
        self.assertIn('CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192',scripted_shared)

    def test_lab12_private_overrides_refresh_stale_sdkconfig_without_relogging_values(self):
        import lab_adapter_lab12 as adapter_module
        import tempfile
        with tempfile.TemporaryDirectory(prefix='lab12-sdkconfig-') as temp:
            build=Path(temp); overlay=build/'private.defaults'; sdkconfig=build/'sdkconfig'
            expected='CONFIG_LAB12_WIFI_SSID="private-ssid"\nCONFIG_LAB12_WIFI_PASSWORD="private-password"\nCONFIG_LAB12_SERVER_URL="http://192.0.2.10:8771"\n'
            overlay.write_text(expected,encoding='utf-8')
            sdkconfig.write_text(expected.replace('private-ssid','').replace('private-password','').replace('192.0.2.10','127.0.0.1'),encoding='utf-8')
            (build/'sdkconfig.old').write_text('stale',encoding='utf-8')
            self.assertTrue(adapter_module._refresh_private_sdkconfig(build,overlay))
            self.assertFalse(sdkconfig.exists()); self.assertFalse((build/'sdkconfig.old').exists())
            sdkconfig.write_text(expected,encoding='utf-8')
            self.assertFalse(adapter_module._refresh_private_sdkconfig(build,overlay))
            self.assertTrue(sdkconfig.exists())

    def test_lab12_scripted_defaults_visual_without_server_or_socket(self):
        import lab_adapter_lab12 as adapter_module
        calls=[]
        def runner(**kwargs): calls.append(kwargs); return 0
        host=SimpleNamespace(ROOT=ROOT,_flash_from_env_and_args=lambda *a,**k:False)
        spec=SimpleNamespace(variants={'manual':1,'scripted':1,'shared':1,'scripted-shared':1},default_variant='manual',capture_seconds_default=30,lab_id='LAB-12',evidence_slug='lab-12',project_dir=PROJECT,target='esp32s3',make_name='lab12')
        args=SimpleNamespace(backend='managed-http',mode='scripted',variant=None,seconds=None,run_id='host-test')
        with patch.object(adapter_module,'run_with_adapter',runner):
            self.assertEqual(adapter_module.Adapter().run(host,None,spec,args,mode='scripted'),0)
        self.assertEqual(calls[0]['variant'],'scripted'); self.assertEqual(calls[0]['mode'],'scripted')
        self.assertFalse(any(p.name=='server.log' for p in ROOT.glob('docs/labs/evidence/lab-12/runs/host-test/*')))
    def test_lab12_backend_rejected_before_io(self):
        import lab_adapter_lab12 as adapter_module
        host=SimpleNamespace(ROOT=ROOT,_flash_from_env_and_args=lambda *a,**k:self.fail('host I/O reached'))
        spec=SimpleNamespace(variants={'manual':1,'scripted':1,'shared':1,'scripted-shared':1},default_variant='manual',capture_seconds_default=30)
        args=SimpleNamespace(backend='local',mode='manual',variant=None,seconds=None,run_id='host-test')
        with self.assertRaises(Exception): adapter_module.Adapter().run(host,None,spec,args,mode='manual')
    def test_lab12_evidence_drops_top_level_experiment_phase(self):
        from lab_run_evidence import EvidenceRun
        import tempfile
        with tempfile.TemporaryDirectory(prefix='lab12-evidence-') as temp:
            root=Path(temp); ev=EvidenceRun(root,'host-test',{},lab_id='LAB-12',evidence_slug='lab-12')
            ev.update_context({'experiment_phase':'visual','capture':{'experiment_phase':'visual','variant':'scripted'}})
            ev.finalize(workflow_status='PASS',experiment_verdict='UNMEASURED',caller_completed=True,cleanup_succeeded=True)
            manifest=json.loads(ev.manifest_path.read_text()); self.assertNotIn('experiment_phase',manifest)
            self.assertEqual(manifest['capture']['experiment_phase'],'visual'); self.assertEqual(manifest['capture']['variant'],'scripted')

class Lab12CaptureGuardTests(unittest.TestCase):
    def test_lab12_serial_redaction_bounds_and_secret_rejection(self):
        import lab_adapter_lab12 as adapter_module
        cleaned=adapter_module._safe_line(b'GET https://fixture.invalid/path password=hunter2 Authorization: Bearer abc123')
        self.assertNotIn('fixture.invalid',cleaned); self.assertNotIn('hunter2',cleaned); self.assertNotIn('abc123',cleaned)
        with self.assertRaises(RuntimeError): adapter_module._safe_line(b'x'*(adapter_module.MAX_LINE+1))
        with self.assertRaises(RuntimeError): adapter_module._safe_line(b'{"payload_b64":"hidden"}')

    def test_lab12_capture_discards_pre_reset_success_marker(self):
        import lab_adapter_lab12 as adapter_module
        import tempfile
        pre=b'LAB12_READY build=build-1 phase=visual server=none layout=split-pane\n'
        class FakeSerial:
            def __init__(self,*a,**k): self.rows=[pre]
            def reset_input_buffer(self): self.rows=[]
            def read(self,n): return self.rows.pop(0) if self.rows else b''
            def close(self): pass
        with tempfile.TemporaryDirectory(prefix='lab12-capture-') as temp:
            host=SimpleNamespace(ROOT=Path(temp))
            target=SimpleNamespace(device='fake-port')
            serial=SimpleNamespace(Serial=FakeSerial)
            clock=iter((0.0,100.0))
            with patch.dict(sys.modules,{'serial':serial}), patch.object(adapter_module.time,'sleep'), patch.object(adapter_module.time,'monotonic',side_effect=lambda:next(clock)):
                with self.assertRaisesRegex(RuntimeError,'LAB12_READY missing'):
                    adapter_module._capture(host,target,'capture-test',1,'visual','scripted','build-1')

    def test_lab12_missing_harness_marker_is_not_complete(self):
        import lab_adapter_lab12 as adapter_module
        text='LAB12_READY build=b phase=visual server=none layout=split-pane\n'
        text+='LAB12_TRACE '+json.dumps({'scenario':'P-preview','operation_id':1,'terminal':'cancelled','terminal_count':1,'saved':0,'success_copy':0})+'\n'
        with self.assertRaisesRegex(TraceError,'harness marker missing'):
            parse_text(text,'host-test','visual','scripted')

if __name__=='__main__':unittest.main()
