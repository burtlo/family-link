"""LAB-03 hooks for the shared v2 run lifecycle. No LAB-02 coordinator imports."""
from __future__ import annotations
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.request import urlopen

from lab_runner import RunHooks, run_with_adapter
import lab03_build

def service_log_line(line):
    """Bounded diagnostics retain context without private addresses or URLs."""
    line=re.sub(r'https?://[^\s]+','[REDACTED]',line[:2048])
    return re.sub(r'\b(?:\d{1,3}\.){3}\d{1,3}\b','[ADDRESS]',line)

class Adapter:
    def run(self, host, config, spec, args, *, mode):
        variant = args.variant or (mode if mode in spec.variants else spec.default_variant)
        seconds = args.seconds if args.seconds is not None else spec.capture_seconds_default
        flash = host._flash_from_env_and_args(args, spec, for_run=True)
        def execute(context, evidence):
            return self.execute(host, config, spec, args, context, evidence, variant, seconds, flash)
        return run_with_adapter(repo_root=host.ROOT, lab_id=spec.lab_id, evidence_slug=spec.evidence_slug,
            variant=variant, mode=mode, requested_run_id=args.run_id,
            hooks=RunHooks(lambda: None, lambda _: None, lambda _t,_r: None),
            project_path=str(spec.project_dir), firmware_target=spec.target, capture_seconds=seconds, execute=execute)

    def execute(self, host, config, spec, args, context, evidence, variant, seconds, flash):
        child = conn = server_log = serial_log = None
        log_thread = None
        read_stop = threading.Event()
        stages = {}
        complete = cleanup = False
        reason = 'preflight_failed'
        serial_path = None
        workflow = 'FAIL'
        experiment = 'INCOMPLETE'
        def stage(name, outcome):
            stages[name] = outcome
            evidence.event_sink({'source':'host','kind':'stage','stage':name,'outcome':outcome,
                                 'monotonic_ns':time.monotonic_ns()})
        try:
            stage('preflight','running')
            target = host.resolve_run_target(config,args.kit,args.port)
            profile = target.deployment.secrets_profile_for_kit(target.kit)
            if not profile or not profile.wifi_ssid or not profile.wifi_password:
                raise ValueError('per-kit transport profile required')
            selected = host.resolve_variant(spec,variant)
            build_dir = host.ROOT/selected.build_dir
            overlay, fingerprint, private = lab03_build.prepare(host,config,spec,selected,build_dir,target=target)
            if getattr(args,'no_build',False):
                raise ValueError('fresh build required for LAB-03 identity proof')
            source = host.ROOT/'server/labs/lab-03-media/server.py'
            fixture_hash = hashlib.sha256((source.parent/'fixtures/manifest.json').read_bytes()).hexdigest()
            server_hash = hashlib.sha256(source.read_bytes()).hexdigest()
            evidence.update_context({'lab_revision':'v2','lab_version_string':'LAB-03/v2',
                'hardware':{'kit_nickname':target.kit_id,'board':'esp32s3-box-3'},
                'firmware':{'build_fingerprint':'sha256:'+fingerprint,'build_id':fingerprint[:24],
                            'source_sha256':lab03_build.source_hash(),'project_path':str(spec.project_dir),'target':spec.target},
                'fixture':{'revision':'lab03-media/v1','sha256':fixture_hash},
                'server':{'revision':'LAB-03/v1','source_sha256':server_hash},
                'scenarios':['fixed-sequence','sustained-600s'] if context.mode=='scripted' else ['manual'],
                'verification_levels':{'build':None,'target':None}})
            stage('preflight','passed')
            stage('service','running')
            server_log = (evidence.run_dir/'server.log').open('w')
            child = subprocess.Popen([sys.executable,str(source),'--bind','0.0.0.0','--port','8766',
                                      '--run-id',context.run_id],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                      text=True,bufsize=1)
            def drain():
                written = 0
                for line in child.stdout:
                    # Service emits only fixed-field JSON; bound diagnostics to 2MiB.
                    if written+len(line)<=2*1024*1024:
                        server_log.write(service_log_line(line)); server_log.flush(); written+=len(line)
            log_thread = threading.Thread(target=drain,daemon=True); log_thread.start()
            deadline = time.monotonic()+10
            while True:
                if child.poll() is not None:
                    raise RuntimeError('owned service exited')
                try:
                    with urlopen('http://127.0.0.1:8766/health',timeout=0.3) as response:
                        health = json.load(response)
                    if health != {'run_id':context.run_id,'fixture_sha256':fixture_hash,
                                  'fixture_version':'lab03-media/v1','server_version':'LAB-03/v1'}:
                        raise RuntimeError('service identity mismatch')
                    break
                except OSError:
                    if time.monotonic()>=deadline: raise RuntimeError('service readiness deadline')
                    time.sleep(0.1)
            evidence.update_context({'artifacts':{'server_log':str((evidence.run_dir/'server.log').relative_to(host.ROOT))}})
            stage('service','passed')
            frozen_source=lab03_build.source_hash()
            stage('build','running')
            defaults = host._sdkconfig_defaults_files(spec.project_dir,selected.sdkconfig_defaults)+[overlay]
            host.build_lab(config,spec.make_name,variant,kit=target.kit_id,target=target,frozen_defaults_files=defaults)
            binary = build_dir/'lab03_media_playback.bin'
            evidence.update_context({'firmware':{'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
                                                'sdkconfig_sha256':hashlib.sha256((build_dir/'sdkconfig').read_bytes()).hexdigest()},
                                     'build':{'status':'built'},'verification_levels':{'build':'L0'}})
            stage('build','passed')
            if lab03_build.source_hash()!=frozen_source:
                raise RuntimeError('source changed during build')
            if flash:
                port = host.revalidate_run_target(config,target)
                host.flash_lab_build(config,build_dir,port)
            import serial
            log_dir = host.ROOT/'logs/device/labs/lab-03'/context.run_id
            log_dir.mkdir(parents=True,exist_ok=False)
            serial_path = log_dir/'serial.txt'
            serial_log = serial_path.open('w')
            evidence.update_context({'artifacts':{'serial_log':str(serial_path.relative_to(host.ROOT))}})
            conn = serial.Serial(target.device,115200,timeout=0.1)
            conn.dtr=False; conn.rts=False; conn.reset_input_buffer()
            armed = threading.Event(); fresh = threading.Event(); ready = threading.Event(); done = threading.Event()
            errors=[]
            expected=f'LAB03_READY build={fingerprint[:24]} fixture=lab03-media/v1 harness={int(variant=="scripted")}'
            def reader():
                pending=b''
                try:
                    while not read_stop.is_set():
                        eligible=fresh.is_set()
                        armed.set()
                        chunk=conn.read(4096)
                        if not chunk: continue
                        pending+=chunk
                        if len(pending)>65536: raise RuntimeError('oversized serial row')
                        while b'\n' in pending:
                            line,pending=pending.split(b'\n',1)
                            text=line.decode('utf-8',errors='replace')
                            for secret in (profile.wifi_ssid,profile.wifi_password,private['LAB03_SERVER_URL']):
                                text=text.replace(secret,'[REDACTED]') if secret else text
                            # No generic SDK endpoints or addresses in evidence.
                            import re
                            text=re.sub(r'https?://[^\s]+','[REDACTED]',text)
                            text=re.sub(r'\b(?:\d{1,3}\.){3}\d{1,3}\b','[ADDRESS]',text)
                            serial_log.write(text+'\n'); serial_log.flush()
                            if eligible and text.startswith(expected) and not ready.is_set():
                                evidence.event_sink({'source':'device','kind':'ready','run_id':context.run_id,
                                    'build_id':fingerprint[:24],'host_received_ns':time.monotonic_ns()})
                                evidence.update_context({'capture':{'status':'ready'},'verification_levels':{'target_startup':'L3'}})
                                ready.set()
                            if eligible and ready.is_set() and text.startswith('LAB03_TRACE '):
                                row=json.loads(text.split('LAB03_TRACE ',1)[1])
                                row.update({'source':'device','kind':'media_trace','run_id':context.run_id,
                                            'host_received_ns':time.monotonic_ns()})
                                # Keep serial authoritative; journal only lifecycle/correlation rows.
                                if row['event'] in {'intent_play','worker_start','terminal_completed','terminal_failed',
                                                    'terminal_cancelled','scenario_step','harness_done'}:
                                    evidence.event_sink(row)
                                if row['event']=='harness_done': done.set()
                                if row['event']=='harness_failed': errors.append('scenario_timeout')
                except Exception as exc: errors.append(type(exc).__name__)
            reader_thread=threading.Thread(target=reader,daemon=True); reader_thread.start()
            if not armed.wait(2): raise RuntimeError('reader not armed')
            stage('activity','running')
            conn.rts=True; time.sleep(0.12); conn.rts=False
            fresh.set()
            if not ready.wait(45): raise RuntimeError('fresh matching ready marker missing')
            print(f'LAB-03 capture active for {seconds}s; synchronized camera/microphone recording is external.',flush=True)
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline and not errors:
                if context.mode=='scripted' and done.is_set():
                    # Allow cancelled workers and terminal trace rows to drain.
                    time.sleep(2); break
                time.sleep(0.2)
            if errors: raise RuntimeError('serial reader failed')
            if context.mode=='scripted' and not done.is_set(): raise RuntimeError('scripted completion missing')
            evidence.update_context({'capture':{'status':'captured','fresh_ready':True}})
            stage('activity','passed')
            stage('scenario','passed' if done.is_set() else 'not_configured')
            complete=True; workflow='PASS'; experiment='UNMEASURED'; reason='capture_completed'
        except (Exception,KeyboardInterrupt) as exc:
            reason=type(exc).__name__
            stage('run','failed')
            print('LAB-03 stopped: '+reason,flush=True)
        finally:
            cleanup=True
            read_stop.set()
            if 'reader_thread' in locals():
                reader_thread.join(2)
                if reader_thread.is_alive(): cleanup=False
            if conn: conn.close()
            if serial_log: serial_log.close()
            if child:
                if child.poll() is None:
                    child.terminate()
                    try: child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill(); child.wait(timeout=2); cleanup=False
                if log_thread:
                    log_thread.join(2)
                    if log_thread.is_alive(): cleanup=False
            if server_log: server_log.close()
            if serial_path and serial_path.exists():
                try:
                    sys.path.insert(0,str(host.ROOT/spec.project_dir/'tools'))
                    from metrics import parse
                    metrics=parse(serial_path.read_text(),context.run_id)
                    path=evidence.run_dir/'metrics.json'
                    path.write_text(json.dumps(metrics,indent=2)+'\n')
                    evidence.update_context({'artifacts':{'metrics':str(path.relative_to(host.ROOT))},
                        'event_loss':{'firmware_trace':metrics['evidence']['firmware_trace_loss']}})
                except Exception:
                    cleanup=False; reason='parse_failed'
            stage('cleanup','passed' if cleanup else 'failed')
        return {'workflow_status':workflow if cleanup else 'FAIL','experiment_verdict':experiment,
                'caller_completed':complete,'cleanup_succeeded':cleanup,'terminal_reason':reason,'stages':stages}
