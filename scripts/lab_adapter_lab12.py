"""LAB-12 phase-aware visual/shared adapter."""
from __future__ import annotations
import hashlib, json, os, re, socket, subprocess, sys, threading, time
from pathlib import Path
from urllib.request import urlopen

from lab_runner import RunHooks, run_with_adapter
from project_config import ProjectConfigError

PORT=8771
MAX_LOG=64*1024
MAX_LINE=4096
MAX_SERIAL=2*1024*1024
READY_TIMEOUT=35
HARNESS_TIMEOUT=240

def _sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()
def _tree_hash(root: Path, fixtures=False) -> str:
    h=hashlib.sha256()
    for p in sorted(root.rglob('*')):
        if not p.is_file(): continue
        rel=p.relative_to(root)
        if any(x.startswith('build') or x in {'managed_components','.git','__pycache__'} for x in rel.parts): continue
        if (rel.parts[0]=='fixtures') != fixtures: continue
        if not fixtures and (p.name=='sdkconfig' or p.suffix in {'.pyc','.o','.a'}): continue
        h.update(rel.as_posix().encode()+b'\0'+p.read_bytes()+b'\0')
    return h.hexdigest()

def _server_fixture_hash(root: Path) -> str:
    h=hashlib.sha256()
    for p in sorted((root/'fixtures').glob('*.json')): h.update(p.name.encode()+b'\0'+p.read_bytes()+b'\0')
    return h.hexdigest()

def _host_address() -> str:
    override=os.environ.get('LAB12_HOST_ADDRESS')
    if override: return override
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    try: sock.connect(('192.0.2.1',9)); return sock.getsockname()[0]
    finally: sock.close()

def _private_overlay(build_dir: Path, profile, server_url: str) -> Path:
    values={'LAB12_WIFI_SSID':profile.wifi_ssid,'LAB12_WIFI_PASSWORD':profile.wifi_password,'LAB12_SERVER_URL':server_url}
    path=build_dir.parent/(build_dir.name+'.lab12.private.defaults')
    def quoted(v): return '"'+v.replace('\\','\\\\').replace('"','\\"')+'"'
    content=''.join(f'CONFIG_{key}={quoted(value)}\n' for key,value in values.items())
    path.parent.mkdir(parents=True,exist_ok=True); path.write_text(content,encoding='utf-8'); os.chmod(path,0o600)
    return path

def _refresh_private_sdkconfig(build_dir: Path, overlay: Path) -> bool:
    """Discard stale generated config when this run's private overrides changed."""
    keys={'CONFIG_LAB12_WIFI_SSID','CONFIG_LAB12_WIFI_PASSWORD','CONFIG_LAB12_SERVER_URL'}
    expected={}
    for line in overlay.read_text(encoding='utf-8').splitlines():
        key,sep,value=line.partition('=')
        if sep and key in keys: expected[key]=value
    if set(expected)!=keys:
        raise RuntimeError('LAB-12 private defaults are incomplete')
    sdkconfig=build_dir/'sdkconfig'
    if not sdkconfig.is_file(): return False
    current={}
    for line in sdkconfig.read_text(encoding='utf-8').splitlines():
        key,sep,value=line.partition('=')
        if sep and key in keys: current[key]=value
    if all(current.get(key)==value for key,value in expected.items()): return False
    sdkconfig.unlink()
    old=build_dir/'sdkconfig.old'
    if old.exists(): old.unlink()
    return True

def _safe_line(raw: bytes) -> str:
    if len(raw)>MAX_LINE: raise RuntimeError('LAB-12 serial line exceeds bound')
    line=raw.decode('utf-8',errors='strict')
    if re.search(r'(?i)"(?:pin|payload_b64|media_payload|image_data|audio_data)"\s*:',line): raise RuntimeError('raw secret or media payload in serial')
    line=re.sub(r'(?i)https?://[^\s"\']+','[REDACTED_URL]',line)
    line=re.sub(r'(?i)(password|passwd)\s*[=:]\s*[^\s,;]+',r'\1=[REDACTED]',line)
    line=re.sub(r'(?i)Bearer\s+\S+','Bearer [REDACTED]',line)
    return line.rstrip('\r')

def _capture(host,target,run_id,seconds,phase,variant,build_id):
    try: import serial
    except ImportError as exc: raise RuntimeError('pyserial is required for LAB-12 capture') from exc
    path=host.ROOT/'logs/device/labs/lab-12'/run_id/'serial.txt'; path.parent.mkdir(parents=True,exist_ok=False)
    conn=serial.Serial(target.device,115200,timeout=.2); conn.dtr=False; conn.rts=True; time.sleep(.1); conn.reset_input_buffer(); conn.rts=False
    ready=done=failed=False; total=0; pending=b''; started=time.monotonic(); deadline=started+READY_TIMEOUT; finish=None
    expected_server='lab12-profile/v1' if phase=='shared' else 'none'
    try:
        with path.open('xb') as log:
            while True:
                chunk=conn.read(2048)
                if chunk:
                    pending+=chunk
                    if len(pending)>MAX_LINE: raise RuntimeError('oversize unterminated LAB-12 serial row')
                    pieces=pending.split(b'\n'); pending=pieces.pop()
                    for piece in pieces:
                        line=_safe_line(piece); out=(line+'\n').encode()
                        if total+len(out)>MAX_SERIAL: raise RuntimeError('LAB-12 serial log exceeds bound')
                        log.write(out); total+=len(out)
                        if line.startswith('LAB12_INIT_FAILED') or line.startswith('LAB12_RUNTIME_FAILED'): failed=True
                        if line.startswith('LAB12_READY '):
                            fields=dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
                            if (ready or set(fields)!={'build','phase','server','layout'} or fields.get('build')!=build_id
                                    or fields.get('phase')!=phase or fields.get('server')!=expected_server
                                    or fields.get('layout')!='split-pane'):
                                failed=True
                            else: ready=True; finish=time.monotonic()+(HARNESS_TIMEOUT if variant.startswith('scripted') else seconds)
                        if line.startswith('LAB12_HARNESS_DONE '):
                            if not variant.startswith('scripted') or line!=f'LAB12_HARNESS_DONE status=complete phase={phase}': failed=True
                            else: done=True
                now=time.monotonic()
                if failed or (variant.startswith('scripted') and done): break
                if not ready and now>=deadline: break
                if finish and now>=finish: break
            if pending:
                line=_safe_line(pending); out=(line+'\n').encode()
                if total+len(out)>MAX_SERIAL: raise RuntimeError('LAB-12 serial log exceeds bound')
                log.write(out)
            log.flush()
        if not ready: raise RuntimeError('post-reset LAB12_READY missing')
        if failed: raise RuntimeError('LAB-12 lifecycle marker failure or mismatch')
        if variant.startswith('scripted') and not done: raise RuntimeError('post-reset LAB12_HARNESS_DONE missing')
        return path
    finally: conn.close()

def _write_server_log(source, dest, stop):
    written=0
    for line in source:
        line=re.sub(r'https?://[^\s]+','[REDACTED_URL]',line[:2048])
        if written<len(line.encode())+written and written+len(line.encode())<=MAX_LOG:
            dest.write(line); dest.flush(); written+=len(line.encode())

class Adapter:
    def run(self,host,config,spec,args,*,mode):
        backend=getattr(args,'backend','managed-http')
        if backend not in (None,'','managed-http'): raise ProjectConfigError('LAB-12 has no selectable backend; rejecting BACKEND before I/O')
        requested=getattr(args,'mode',None) or mode
        if requested not in {'manual','scripted'}: raise ProjectConfigError('LAB-12 mode must be manual or scripted')
        variant=args.variant or (requested if requested in spec.variants else spec.default_variant)
        if variant not in spec.variants: raise ProjectConfigError(f'unknown LAB-12 variant {variant}')
        phase='shared' if variant in {'shared','scripted-shared'} else 'visual'
        if requested=='manual' and variant not in {'manual','shared'}: raise ProjectConfigError('LAB-12 manual mode/variant mismatch')
        if requested=='scripted' and variant not in {'scripted','scripted-shared'}: raise ProjectConfigError('LAB-12 scripted mode/variant mismatch')
        seconds=args.seconds if args.seconds is not None else spec.capture_seconds_default
        if not 1<=seconds<=900: raise ProjectConfigError('LAB-12 capture duration outside 1..900 seconds')
        flash=host._flash_from_env_and_args(args,spec,for_run=True)
        def execute(context,evidence): return self._execute(host,config,spec,args,context,evidence,requested,variant,phase,seconds,flash)
        return run_with_adapter(repo_root=host.ROOT,lab_id=spec.lab_id,evidence_slug=spec.evidence_slug,variant=variant,mode=requested,requested_run_id=args.run_id,
            hooks=RunHooks(lambda:None,lambda _t:None,lambda _t,_r:None),project_path=str(spec.project_dir),firmware_target=spec.target,capture_seconds=seconds,execute=execute)

    def _execute(self,host,config,spec,args,context,evidence,mode,variant,phase,seconds,flash):
        child=log_file=log_thread=None; target=None; stages={k:'not_started' for k in ('preflight','service','build','activity','parser','scenario','cleanup')}
        stages['service']='not_configured' if phase=='visual' else 'not_started'; complete=cleanup=False; workflow='FAIL'; experiment='UNMEASURED'; reason='run_failed'
        server_root=host.ROOT/'server/labs/lab-12-profile-settings'; fixture_hash=_server_fixture_hash(server_root)
        def stage(name,outcome):
            stages[name]=outcome; evidence.event_sink({'source':'host','kind':'stage','stage':name,'outcome':outcome,'monotonic_ns':time.monotonic_ns()})
        try:
            stage('preflight','running')
            target=host.resolve_run_target(config,args.kit,args.port)
            resolved=host.resolve_variant(spec,variant); build_dir=host.ROOT/resolved.build_dir
            source_hash=_tree_hash(spec.project_dir); fixture_source_hash=_tree_hash(spec.project_dir,True)
            evidence.update_context({'firmware':{'project_path':str(spec.project_dir),'target':spec.target},'server':{'path':'none' if phase=='visual' else 'server/labs/lab-12-profile-settings','revision':'LAB-12/v1' if phase=='shared' else None},
                'fixture':{'revision':'lab12-profile/v1','schema':'lab12-profile/v1','sha256':fixture_hash},
                'capture':{'experiment_phase':phase,'variant':variant,'mode':mode,'seconds':seconds},
                'scenarios':['P-preview','P-modal','P-return'] if phase=='visual' else ['P-save-fail','P-conflict','P-lost','P-invalid','P-hint'],
                'verification_levels':{'build':None,'target':None}})
            stage('preflight','passed')
            if phase=='shared':
                stage('service','running'); log_path=evidence.run_dir/'server.log'; log_file=log_path.open('w',encoding='utf-8')
                server_file=server_root/'server.py'; server_hash=_sha(server_file)
                child=subprocess.Popen([sys.executable,str(server_file),'--bind','0.0.0.0','--port',str(PORT),'--run-id',context.run_id],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,bufsize=1)
                def drain():
                    count=0
                    for line in child.stdout:
                        safe=re.sub(r'https?://[^\s]+','[REDACTED_URL]',line[:2048]); b=safe.encode()
                        if count+len(b)<=MAX_LOG: log_file.write(safe); log_file.flush(); count+=len(b)
                log_thread=threading.Thread(target=drain,daemon=True); log_thread.start()
                deadline=time.monotonic()+10
                while True:
                    if child.poll() is not None: raise RuntimeError('owned LAB-12 fixture exited')
                    try:
                        with urlopen(f'http://127.0.0.1:{PORT}/health',timeout=.3) as response: health=json.load(response)
                        if health!={'run_id':context.run_id,'fixture_sha256':fixture_hash,'fixture_version':'lab12-profile/v1','server_version':'LAB-12/v1'}: raise RuntimeError('LAB-12 fixture identity mismatch')
                        break
                    except OSError:
                        if time.monotonic()>=deadline: raise RuntimeError('LAB-12 fixture startup deadline')
                        time.sleep(.1)
                evidence.update_context({'server':{'path':'server/labs/lab-12-profile-settings','revision':'LAB-12/v1','source_sha256':server_hash},'artifacts':{'server_log':log_path.relative_to(host.ROOT).as_posix()}})
                stage('service','passed')
            stage('build','running')
            if getattr(args,'no_build',False): raise RuntimeError('LAB-12 requires registered build for run identity')
            selected_defaults=host._sdkconfig_defaults_files(spec.project_dir,resolved.sdkconfig_defaults)
            if phase=='shared':
                profile=target.deployment.secrets_profile_for_kit(target.kit)
                if not profile or not profile.wifi_ssid or not profile.wifi_password: raise RuntimeError('shared phase requires kit Wi-Fi profile')
                overlay=_private_overlay(build_dir,profile,f'http://{_host_address()}:{PORT}')
                _refresh_private_sdkconfig(build_dir,overlay)
                selected_defaults.append(overlay)
            result=host.build_lab(config,spec.make_name,variant,kit=target.kit_id,port=target.device,target=target,frozen_defaults_files=selected_defaults)
            sdk=build_dir/'sdkconfig'; binary=build_dir/'lab12_profile_settings.bin'
            cfg=sdk.read_text(encoding='utf-8')
            is_scripted=variant.startswith('scripted'); is_shared=phase=='shared'
            if ('CONFIG_LAB12_SCRIPTED=y' in cfg.splitlines())!=is_scripted or ('CONFIG_LAB12_PHASE_SHARED=y' in cfg.splitlines())!=is_shared: raise RuntimeError('resolved LAB-12 sdkconfig variant mismatch')
            build_id=re.search(r'^CONFIG_LAB12_BUILD_ID="([^"\r\n]+)"$',cfg,re.M)
            if not build_id or not binary.is_file(): raise RuntimeError('LAB-12 build identity output missing')
            identity={'build_id':build_id.group(1),'variant':variant,'source_sha256':source_hash,'sdkconfig_sha256':_sha(sdk),'binary_sha256':_sha(binary)}
            identity['build_fingerprint']='sha256:'+hashlib.sha256(json.dumps(identity,sort_keys=True,separators=(',',':')).encode()).hexdigest()
            if source_hash!=_tree_hash(spec.project_dir) or fixture_source_hash!=_tree_hash(spec.project_dir,True): raise RuntimeError('LAB-12 sources changed during build')
            evidence.update_context({'firmware':identity,'build':{'status':'built'},'verification_levels':{'build':'L0'}}); stage('build','passed')
            if flash:
                port=host.revalidate_run_target(config,target); host.flash_lab_build(config,result,port); time.sleep(1)
            stage('activity','running')
            serial_path=_capture(host,target,context.run_id,seconds,phase,variant,build_id.group(1))
            evidence.update_context({'capture':{'status':'captured','post_reset':True},'artifacts':{'serial_log':serial_path.relative_to(host.ROOT).as_posix()}}); stage('activity','passed')
            stage('parser','running')
            sys.path.insert(0,str(spec.project_dir/'tools'))
            from parse_lab_trace import parse_file
            metrics=parse_file(serial_path,output_path=evidence.run_dir/'metrics.json',expected_run_id=context.run_id,expected_phase=phase,expected_variant=variant,post_reset=True)
            experiment=metrics['lab_metrics']['experiment_verdict']
            evidence.update_context({'artifacts':{'metrics':(evidence.run_dir/'metrics.json').relative_to(host.ROOT).as_posix()},'event_loss':{'firmware_trace':{'status':'UNKNOWN','count':None}}}); stage('parser','passed')
            stage('scenario','passed' if metrics['lab_metrics']['harness_complete'] else 'not_configured')
            complete=True; workflow='PASS'; reason='post_reset_phase_harness_complete' if is_scripted else 'post_reset_manual_capture_complete'
        except KeyboardInterrupt: workflow='INCOMPLETE'; reason='KeyboardInterrupt'
        except Exception as exc: workflow='FAIL'; reason=type(exc).__name__; stage('run','failed')
        finally:
            cleanup=True
            if child is not None:
                if child.poll() is None:
                    child.terminate()
                    try: child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        try: child.wait(timeout=2)
                        except Exception: cleanup=False
                if log_thread:
                    log_thread.join(2)
                    if log_thread.is_alive(): cleanup=False
            if log_file:
                try: log_file.close()
                except Exception: cleanup=False
            stages['cleanup']='passed' if cleanup else 'failed'; stage('cleanup',stages['cleanup'])
            if not cleanup: workflow='FAIL'; complete=False; reason='cleanup_failed'
            evidence.update_context({'capture':{'stages':stages}})
        return {'workflow_status':workflow,'experiment_verdict':experiment if workflow=='PASS' else ('INCOMPLETE' if experiment!='UNMEASURED' else 'UNMEASURED'),'caller_completed':complete,'cleanup_succeeded':cleanup,'terminal_reason':reason,'stages':stages}
