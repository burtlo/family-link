import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from urllib.request import urlopen
from urllib.error import HTTPError

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from lab_registry import get_lab
from lab_adapter_registry import resolve_adapter

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    value=importlib.util.module_from_spec(spec);spec.loader.exec_module(value)
    return value

class Lab03Test(unittest.TestCase):
    def test_fixture_regeneration_is_byte_identical(self):
        generator=module('lab03_fixture_generator',ROOT/'server/labs/lab-03-media/generate_fixtures.py')
        with tempfile.TemporaryDirectory() as td:
            generator.ROOT=Path(td);generator.generate()
            for item in generator.ROOT.iterdir():
                expected=ROOT/'server/labs/lab-03-media/fixtures'/item.name
                self.assertEqual(item.read_bytes(),expected.read_bytes())

    def test_build_identity_uses_compiled_header_without_sdkconfig_churn(self):
        import lab03_build
        from unittest.mock import patch
        spec=get_lab('lab03');variant=spec.variants['manual']
        with tempfile.TemporaryDirectory() as td, patch.object(lab03_build,'client_address',return_value='127.0.0.1'):
            build=Path(td)/'build'
            overlay,identity,_private=lab03_build.prepare(None,None,spec,variant,build)
            original=overlay.read_bytes();header=(build/'lab03_build_id.h').read_text()
            self.assertIn(identity[:24],header)
            self.assertNotIn('CONFIG_LAB03_BUILD_ID',original.decode())
            with patch.object(lab03_build,'source_hash',return_value='changed-source'):
                again,new_identity,_private=lab03_build.prepare(None,None,spec,variant,build)
            self.assertNotEqual(identity,new_identity)
            self.assertEqual(original,again.read_bytes())
            self.assertIn(new_identity[:24],(build/'lab03_build_id.h').read_text())
            import shutil
            shutil.rmtree(build)
            lab03_build.restore_header(build)
            self.assertIn(new_identity[:24],(build/'lab03_build_id.h').read_text())

    def test_parser_excludes_pre_reset_rows_and_detects_reboot(self):
        parser=module('lab03_metrics_scope',ROOT/'client/labs/lab-03-media-playback/tools/metrics.py')
        def row(value):
            return 'LAB03_TRACE '+json.dumps(dict(event='callback_us',t_us=value,operation_id=1,
               session_id=1,message_id='A',position_ms=0,rendered=0,value=value,aux=0))
        ready='LAB03_READY build=expected fixture=lab03-media/v1 harness=1'
        result=parser.parse(row(9999)+'\n'+ready+'\n'+row(12),'unit')
        self.assertEqual(result['lab_metrics']['callback_us']['worst'],12)
        self.assertEqual(result['lab_metrics']['pre_ready_trace_rows_excluded'],1)
        self.assertIn('unexpected_device_reboot',parser.parse(ready+'\n'+ready,'unit')['evidence']['missing'])

    def test_registry_dispatch_and_parser_cli(self):
        spec=get_lab('lab03')
        self.assertEqual(spec.adapter_key,'lab03')
        self.assertEqual(type(resolve_adapter(spec)).__module__,'lab_adapter_lab03')
        self.assertEqual(set(spec.variants),{'manual','scripted'})
        with tempfile.TemporaryDirectory() as td:
            serial=Path(td)/'serial.txt';serial.write_text('')
            out=Path(td)/'metrics.json'
            subprocess.run([sys.executable,str(ROOT/spec.trace_parser),str(serial),'-o',str(out)],check=True)
            self.assertEqual(json.loads(out.read_text())['document'],'family-link.lab-metrics/v2')

    def test_native_dense_replay(self):
        project=ROOT/'client/labs/lab-03-media-playback'
        with tempfile.TemporaryDirectory() as td:
            exe=Path(td)/'core'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(project/'main'),
                            str(project/'tests/core_test.c'),str(project/'main/media_core.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe),str(ROOT/'server/labs/lab-03-media/fixtures/dense.sketch')],check=True)

    def test_fixed_service_identity_bytes_delays_and_failure(self):
        service=module('lab03_fixture_test',ROOT/'server/labs/lab-03-media/server.py')
        http=service.Server(('127.0.0.1',0),service.Handler);http.run_id='unit-service'
        thread=threading.Thread(target=http.serve_forever,daemon=True);thread.start()
        base=f'http://127.0.0.1:{http.server_port}'
        try:
            with urlopen(base+'/health') as response: health=json.load(response)
            self.assertEqual(health['run_id'],'unit-service')
            self.assertEqual(health['fixture_sha256'],service.FIXTURE_HASH)
            for mid in 'ABC':
                for asset in ('audio','sketch'):
                    name=service.MANIFEST['messages'][mid][asset]
                    if not name: continue
                    with urlopen(base+f'/media/{mid}/{asset}') as response: data=response.read()
                    self.assertEqual(hashlib.sha256(data).hexdigest(),service.MANIFEST['assets'][name]['sha256'])
            with self.assertRaises(HTTPError) as failure: urlopen(base+'/media/D/audio')
            self.assertEqual(failure.exception.code,503)
            failure.exception.close()
            with urlopen(base+'/inbox?page=1') as response: self.assertEqual(json.load(response)['items'][0]['message_id'],'C')
        finally:
            http.shutdown();http.server_close();thread.join()

    def test_metrics_missing_and_duplicate_terminals(self):
        parser=module('lab03_metrics_test',ROOT/'client/labs/lab-03-media-playback/tools/metrics.py')
        empty=parser.parse('','unit')
        self.assertIsNone(empty['lab_metrics']['audio_underruns'])
        self.assertIsNone(empty['lab_metrics']['callback_us']['p95'])
        self.assertEqual(empty['evidence']['trace_loss'],'UNKNOWN')
        def row(event,t=1,**kw):
            return 'LAB03_TRACE '+json.dumps(dict(event=event,t_us=t,operation_id=1,session_id=1,
              message_id='A',position_ms=100,rendered=0,value=0,aux=0,**kw))
        text='\n'.join([row('intent_play'),row('worker_start'),row('terminal_cancelled'),row('terminal_cancelled')])
        result=parser.parse(text,'unit')
        self.assertFalse(result['lab_metrics']['exactly_one_terminal'])
        self.assertEqual(result['lab_metrics']['terminal_events']['1']['count'],2)

    def test_completed_operation_stop_is_not_negative_cancellation(self):
        parser=module('lab03_metrics_cancel',ROOT/'client/labs/lab-03-media-playback/tools/metrics.py')
        rows=[]
        for event,t in [('intent_play',1),('worker_start',2),('terminal_completed',100),
                        ('stop_request',200),('stop_ui_effect',201)]:
            rows.append('LAB03_TRACE '+json.dumps(dict(event=event,t_us=t,operation_id=1,
                session_id=1,message_id='A',position_ms=3000,rendered=12,value=0,aux=0)))
        result=parser.parse('\n'.join(rows),'unit')['lab_metrics']
        self.assertEqual(result['cancel_to_worker_terminal_us']['n'],0)
        self.assertEqual(result['cancel_to_ui_us']['worst'],1)
        self.assertEqual(len(result['inactive_or_unpaired_stops']),1)

    def test_readiness_and_later_focus_do_not_prove_B_playback(self):
        parser=module('lab03_metrics_coverage',ROOT/'client/labs/lab-03-media-playback/tools/metrics.py')
        rows=[]
        for event,t,value in [('media_ready',10,4096),('focus',20,0)]:
            rows.append('LAB03_TRACE '+json.dumps(dict(event=event,t_us=t,operation_id=1,
                session_id=1,message_id='B',position_ms=0,rendered=0,value=value,aux=0)))
        result=parser.parse('\n'.join(rows),'unit')['lab_metrics']['scenarios_observed']
        self.assertFalse(result['focus_B_play_B'])
        self.assertFalse(result['maximum_dense_sketch'])

    def test_parser_sparse_gap_and_percentiles(self):
        parser=module('lab03_metrics_sparse',ROOT/'client/labs/lab-03-media-playback/tools/metrics.py')
        rows=[]
        for event,progress,value,aux in [('reveal_batch',0,1,12),('trace_loss',0,0,0)]:
            rows.append('LAB03_TRACE '+json.dumps(dict(event=event,t_us=1,operation_id=1,
               session_id=1,message_id='A',position_ms=20,rendered=progress,value=value,aux=aux)))
        result=parser.parse('\n'.join(rows),'unit')
        self.assertEqual(result['lab_metrics']['reveal_lateness_ms']['n'],1)
        self.assertEqual(result['lab_metrics']['reveal_lateness_ms']['worst'],20)
        self.assertEqual(parser.distribution([1,2,100])['p99'],100)

if __name__=='__main__': unittest.main()
