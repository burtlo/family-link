#!/usr/bin/env python3
"""Private, fail-closed H35 controller. Discovery never authorizes media writes.

Private epochs live under ``~/family-link-storage-experiments/``. Suggested new
run directory: ``h35-YYYYMMDD`` (see ``attached_storage_paths.default_h35_run_dir``).
After a successful capture, point H37/H38 at it via ``--h35-capture-dir``,
``FAMILY_LINK_H35_CAPTURE_DIR``, ``current-h35-capture.json``, or ``parse --set-current``.
"""
from __future__ import annotations
import argparse,importlib,importlib.metadata,json,os,re,secrets,subprocess,sys,time
import tempfile
from pathlib import Path
import h32_storage_qual as h32
from attached_storage_paths import (
    DEFAULT_BOX_BACKUP_DIR,
    default_h35_run_dir,
    write_current_h35_capture_pointer,
)
ROOT=h32.ROOT; FW=h32.FW
stop=h32.stop; digest=h32.digest; atomic_json=h32.atomic_json
CAPTURE_USED_MARKERS=('flash-attempt-private.json','capture-private.txt',
                      'host-preflight-private.json','capture-ready-private.json')


def private(path):
    if path is None or path.resolve().is_relative_to(ROOT.resolve()) or path.is_symlink():
        stop('private directory outside repository required')
    if path.exists() and path.stat().st_mode & 0o077: stop('private directory permissions must be 0700')
    return path.resolve()


def backup(path):
    private(path); b=h32.require_backup(path)
    if b['full']['bytes']!=h32.FLASH or b['device'].get('chip')!='esp32-s3' or b['device'].get('flash_bytes')!=h32.FLASH:
        stop('backup must be complete ESP32-S3 16 MiB image')
    return b


def source_identity():
    inputs=[FW/'CMakeLists.txt',FW/'main/CMakeLists.txt',FW/'main/idf_component.yml',FW/'dependencies.lock',FW/'sdkconfig.defaults',FW/'sdkconfig.h35.defaults',FW/'partitions/h35_sdmmc_discovery.csv',FW/'demos/h35_sdmmc_discovery.c',Path(__file__).resolve(),ROOT/'scripts/h32_storage_qual.py']
    inputs += sorted((FW/'managed_components/espressif__esp-box-3').rglob('*.h'))
    inputs += sorted((FW/'managed_components/espressif__esp-box-3').rglob('*.c'))
    inputs += sorted((FW/'main').glob('*.c'))
    inputs += sorted((FW/'main').glob('*.h'))
    files={str(p.relative_to(ROOT)):digest(p) for p in inputs}
    return {'files':files,'sha256':h32.dh(json.dumps(files,sort_keys=True).encode())}


def require_run(rd,bd=None):
    rd=private(rd); m=json.loads((rd/'run-private.json').read_text())
    if m.get('schema')!=1 or not h32.EPOCH_RE.fullmatch(m.get('epoch','')) or m.get('mode')!='sdmmc_read_only': stop('invalid H35 epoch')
    if Path(m['build_dir']).resolve()!=rd/'build': stop('build directory differs from private epoch')
    if bd is not None:
        b=backup(bd)
        if m['original_full_sha256']!=b['full']['sha256'] or m['device_fingerprint_sha256']!=b['device']['fingerprint_sha256']: stop('backup binding differs')
    return m


def prepare(rd,bd):
    rd=private(rd); b=backup(bd)
    if rd.exists(): stop('immutable run directory already exists')
    # A backup may be archived; freshness is established by a full connected reread before flashing.
    rd.mkdir(parents=True,mode=0o700)
    m={'schema':1,'epoch':secrets.token_hex(16),'mode':'sdmmc_read_only','created_unix':int(time.time()),'build_dir':str(rd/'build'),'backup_dir':str(bd.resolve()),'original_full_sha256':b['full']['sha256'],'device_fingerprint_sha256':b['device']['fingerprint_sha256']}
    atomic_json(rd/'run-private.json',m,0o600)
    print(json.dumps({'status':'prepared','epoch':m['epoch']}))


def build(rd,bd):
    m=require_run(rd,bd); b=backup(bd); d=Path(m['build_dir'])
    if d.exists(): stop('build directory already exists; prepare a new epoch')
    before=source_identity(); d.mkdir(mode=0o700)
    ps=b['partition_table']['entries']
    if any(p['flags'] not in (0,1) for p in ps): stop('unsupported original partition flags')
    factory=[h32.qual_app_partition(ps)]
    app_off=factory[0]['offset']
    csv=d/'original-partitions.csv'
    csv.write_text('# Original private backup partition map\n'+''.join(f"{p['name']},0x{p['type']:x},0x{p['subtype']:x},0x{p['offset']:x},0x{p['size']:x},{'encrypted' if p['flags']==1 else ''}\n" for p in ps))
    defaults=d/'partition.defaults'
    defaults.write_text(f'CONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{csv}"\nCONFIG_PARTITION_TABLE_OFFSET=0x8000\n')
    h32.run(['idf.py','-D','FAMILY_DEMO=h35_sdmmc_discovery','-D',f"H35_RUN_EPOCH={m['epoch']}",'-D',f'SDKCONFIG={d}/sdkconfig','-D',f'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.h35.defaults;{defaults}','-B',str(d),'-C',str(FW),'build'])
    if before!=source_identity(): stop('source changed during compilation')
    snap=d/'source-snapshot'
    for rel,sha in before['files'].items():
        p=snap/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/rel).read_bytes())
        if digest(p)!=sha: stop('source snapshot changed')
    images={n:{'path':str(p),'bytes':p.stat().st_size,'sha256':digest(p),'offset':off} for n,p,off in [('app',d/'family_link_demo.bin',app_off),('partition_table',d/'partition_table/partition-table.bin',h32.PT_OFF),('bootloader',d/'bootloader/bootloader.bin',0)]}
    sdk_files={str(p.relative_to(h32.IDF)):digest(p) for p in [h32.IDF/'tools/cmake/version.cmake',h32.IDF/'components/esp_system/include/esp_system.h',h32.IDF/'components/sdmmc/sdmmc_cmd.c',h32.IDF/'components/esp_driver_sdmmc/src/sdmmc_host.c']}
    manifest={'schema':1,'epoch':m['epoch'],'source':before,'sdk':sdk_files,'idf_version':h32.idf_version(),'git_commit':h32.git_head(),'sdkconfig_sha256':digest(d/'sdkconfig'),'partition_csv_sha256':digest(csv),'partitions':h32.partitions((d/'partition_table/partition-table.bin').read_bytes()),'images':images,'elf_sha256':digest(d/'family_link_demo.elf'),'app_descriptor':h32.app_desc((d/'family_link_demo.bin').read_bytes()),'factory':factory[0]}
    atomic_json(d/'manifest.json',manifest,0o600); validate(rd,bd)


def validate(rd,bd,historical=False):
    m=require_run(rd,bd); b=backup(bd); d=Path(m['build_dir']); v=json.loads((d/'manifest.json').read_text())
    if v['epoch']!=m['epoch'] or v['schema']!=1: stop('manifest epoch/schema differs')
    if h32.idf_version()!=v['idf_version']: stop('IDF version differs')
    if not historical and v['source']!=source_identity(): stop('source changed; new epoch required')
    for rel,sha in v['source']['files'].items():
        if digest(d/'source-snapshot'/rel)!=sha: stop('source snapshot mismatch')
    for rel,sha in v['sdk'].items():
        if digest(h32.IDF/rel)!=sha: stop('SDK source mismatch')
    if digest(d/'sdkconfig')!=v['sdkconfig_sha256'] or digest(d/'original-partitions.csv')!=v['partition_csv_sha256']: stop('config mismatch')
    for key,x in v['images'].items():
        p=Path(x['path'])
        if not p.resolve().is_relative_to(d.resolve()) or p.stat().st_size!=x['bytes'] or digest(p)!=x['sha256']: stop('image binding mismatch')
    ps=h32.partitions(Path(v['images']['partition_table']['path']).read_bytes())
    if ps!=b['partition_table']['entries'] or ps!=v['partitions']: stop('partition layout differs from original')
    factory=h32.qual_app_partition(ps)
    if factory!=v['factory'] or v['images']['app']['offset']!=factory['offset'] or v['images']['app']['bytes']>factory['size']*85//100: stop('app slot bounds/headroom invalid')
    if digest(d/'family_link_demo.elf')!=v['elf_sha256'] or h32.app_desc(Path(v['images']['app']['path']).read_bytes())!=v['app_descriptor'] or v['app_descriptor']['elf_sha256']!=v['elf_sha256']: stop('ELF descriptor binding differs')
    config=(d/'sdkconfig').read_text()
    if 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' not in config or 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' not in config: stop('required native USB/16MB configuration missing')
    return v

FIELDS={
 'BOOT':{'reset_reason','elf_sha256'},
 'TRANSPORT':{'backend','mode','console','usb_host','slot','width_requested','max_freq_khz','command_timeout_ms','deadline_ms'},
 'POWER':{'gpio','active_level','error'},'HOST':{'error'},'SLOT':{'error'},'CARD':{'error'},
 'GEOMETRY':{'sectors','sector_bytes','capacity_bytes','bus_width','real_freq_khz','card_max_freq_khz','ddr'},
 'IDENTITY':{'scheme','sha256'},'STOP':{'reason','error','deinit_error','power_off_error'},
 'COMPLETE':{'result','card_present','scope'}}


def parse_records(text,epoch,elf):
    ordered=[]
    for line in text.splitlines():
        if 'H35' not in line: continue
        at=line.find('H35,'); parts=line[at:].split(',')
        if at<0 or len(parts)<4 or parts[:2]!=['H35','1'] or parts[2] not in FIELDS: stop('unknown/malformed H35 record')
        event=parts[2]; fields=h32.kv(','.join(parts[3:]))
        if fields.pop('epoch',None)!=epoch: stop('record epoch mismatch')
        allowed=FIELDS[event]
        if event=='STOP' and fields.get('reason')!='finished': allowed={'reason','error'}
        if event=='COMPLETE' and fields.get('result') in ('timeout','error') and 'card_present' not in fields: allowed={'result','scope'}
        if set(fields)!=allowed: stop('unexpected/missing record fields: '+event)
        if event in [e for e,_ in ordered]: stop('duplicate event: '+event)
        ordered.append((event,fields))
    names=[e for e,_ in ordered]; r=dict(ordered)
    if names[:2]!=['BOOT','TRANSPORT'] or names[-2:]!=['STOP','COMPLETE']: stop('incomplete discovery sequence')
    # Pinned ESP-IDF 5.4.2 esp_system.h: INT_WDT=5, TASK_WDT=6, WDT=7.
    if r['BOOT']['reset_reason'] in ('5','6','7'): stop('watchdog reset invalidates discovery evidence')
    if r['BOOT']['elf_sha256']!=elf or not re.fullmatch('[0-9a-f]{64}',elf): stop('runtime ELF mismatch')
    expected={'backend':'sdmmc','mode':'read_only','console':'usb_serial_jtag','usb_host':'disabled','slot':'0','width_requested':'4','max_freq_khz':'20000','command_timeout_ms':'1000','deadline_ms':'30000'}
    if r['TRANSPORT']!=expected: stop('transport contract differs')
    if r['COMPLETE']['scope']!='discovery_only': stop('invalid scope')
    result=r['COMPLETE']['result']
    if result not in ('detected','init_failed','unsupported','error','timeout'): stop('unknown terminal result')
    if r['STOP']['reason'] not in ('finished','deadline','allocation'): stop('unknown stop reason')
    for event,fields in ordered:
        numeric=FIELDS[event]-{'elf_sha256','backend','mode','console','usb_host','scheme','sha256','reason','result','card_present','scope'}
        if any(k in fields and not re.fullmatch('-?[0-9]+',fields[k]) for k in numeric): stop('noninteger record field')
    if r['STOP']['reason']=='allocation' and (result!='error' or r['STOP']['error']!='257' or names!=['BOOT','TRANSPORT','STOP','COMPLETE']): stop('invalid allocation terminal')
    if r['STOP']['reason']=='deadline' and result!='timeout': stop('deadline without timeout result')
    if r['STOP']['reason']=='finished' and 'POWER' not in r: stop('missing discovery stages')
    expected_order=['BOOT','TRANSPORT','POWER','HOST','SLOT','CARD','GEOMETRY','IDENTITY','STOP','COMPLETE']
    if names!=[n for n in expected_order if n in names]: stop('events out of order')
    for prior,next_event in [('POWER','HOST'),('HOST','SLOT'),('SLOT','CARD'),('CARD','GEOMETRY')]:
        if next_event in r and (prior not in r or r[prior]['error']!='0'): stop('event followed failed/missing prerequisite')
    if 'POWER' in r and (r['POWER']['gpio']!='43' or r['POWER']['active_level']!='0'): stop('unapproved power configuration')
    if r['STOP']['reason']=='finished':
        for prior,next_event in [('POWER','HOST'),('HOST','SLOT'),('SLOT','CARD')]:
            if prior in r and r[prior]['error']=='0' and next_event not in r: stop('successful stage missing its next stage')
        if result=='init_failed':
            stage=next((e for e in ('POWER','HOST','SLOT','CARD') if e in r and r[e]['error']!='0'),None)
            if stage is None or r['STOP']['error']!=r[stage]['error']: stop('init_failed lacks matching driver error')
        if result=='unsupported' and ('CARD' not in r or r['CARD']['error']!='0' or r['STOP']['error']!='262'): stop('unsupported lacks initialized card proof')
    if result=='detected':
        if names!=expected_order or r['COMPLETE']['card_present']!='proven' or any(r[e]['error']!='0' for e in ('POWER','HOST','SLOT','CARD','STOP')) or r['STOP']['deinit_error']!='0' or r['STOP']['power_off_error']!='0': stop('invalid detected terminal proof')
        g=r['GEOMETRY']
        try: g={k:int(v) for k,v in g.items()}
        except ValueError: stop('noninteger geometry')
        if g['sectors']<=0 or g['sector_bytes']!=512 or g['capacity_bytes']!=g['sectors']*512 or g['bus_width'] not in (1,4) or not 0<g['real_freq_khz']<=20000 or g['card_max_freq_khz']<=0 or g['ddr']!=0: stop('invalid geometry')
        if r['IDENTITY']['scheme']!='epoch_sha256_decoded_cid_v1' or not re.fullmatch('[0-9a-f]{64}',r['IDENTITY']['sha256']): stop('invalid private identity digest')
    elif r['COMPLETE'].get('card_present','unproven')!='unproven': stop('failure claimed card presence')
    if result=='timeout' and (r['STOP']['reason']!='deadline' or r['STOP']['error']!='263'): stop('invalid timeout')
    if result in ('init_failed','unsupported') and (r['STOP']['reason']!='finished' or r['STOP']['error']=='0' or 'IDENTITY' in r): stop('invalid failure terminal')
    return {'status':'present' if result=='detected' else 'inconclusive','result':result,'scope':'discovery_only','qualification':'unqualified','records':r}


def resolve_serial(importer=importlib.import_module):
    """Resolve and exercise the required pyserial API without opening a port."""
    try:
        serial=importer('serial')
    except Exception:
        return None,{'status':'failed','reason':'serial_import_failed'},'serial import failed'
    try:
        dist=importlib.metadata.distribution('pyserial')
        version=dist.version
        match=re.match(r'^(\d+)\.(\d+)',version)
        if not match or (int(match[1]),int(match[2]))<(3,5):
            raise ValueError('unsupported pyserial version')
        owners={re.sub(r'[-_.]+','-',x).lower() for x in (importlib.metadata.packages_distributions().get('serial') or [])}
        if 'pyserial' not in owners:
            raise ValueError('serial module is not owned by pyserial')
        module_path=Path(serial.__file__).resolve()
        package_files=dist.files or ()
        expected={Path(dist.locate_file(item)).resolve() for item in package_files
                  if item.as_posix().endswith('serial/__init__.py')}
        if module_path not in expected:
            raise ValueError('serial module does not match pyserial distribution')
        ctor=getattr(serial,'Serial',None)
        if not callable(ctor):
            raise ValueError('Serial constructor missing')
        port=ctor(port=None,baudrate=115200,timeout=.2)
        try:
            if getattr(port,'is_open',True) or getattr(port,'port','not-none') is not None:
                raise ValueError('unconnected Serial unexpectedly opened')
            port.dtr=False
            port.rts=False
            port.port=None
            for name in ('reset_input_buffer','read','close','__enter__','__exit__'):
                if not callable(getattr(port,name,None)):
                    raise ValueError('required Serial API missing')
        finally:
            port.close()
        info={'status':'pass','distribution':'pyserial','version':version,
              'module_path':str(module_path),'distribution_path':str(Path(dist.locate_file('')).resolve()),
              'api':['Serial(port=None)','dtr','rts','port','reset_input_buffer','read','close','context_manager']}
        return serial,info,None
    except Exception:
        return None,{'status':'failed','reason':'pyserial_distribution_or_api_invalid'},'pyserial distribution or required API invalid'


def _write_host_preflight(rd,info):
    target=rd/'host-preflight-private.json'
    data={'schema':1,'status':info['status'],'python_executable':sys.executable,
          'python_version':sys.version,'python_prefix':sys.prefix,'python_base_prefix':sys.base_prefix,
          'serial':info}
    # One immutable attempt per run. This file is private run metadata and is
    # never copied to sanitized public evidence.
    fd=os.open(target,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    try:
        with os.fdopen(fd,'w') as f:
            json.dump(data,f,sort_keys=True);f.write('\n');f.flush();os.fsync(f.fileno())
        dfd=os.open(rd,os.O_RDONLY)
        try:os.fsync(dfd)
        finally:os.close(dfd)
    except BaseException:
        try:target.unlink()
        except OSError:pass
        raise


def capture_preflight(rd,importer=importlib.import_module):
    rd=private(rd)
    serial,info,error=resolve_serial(importer)
    _write_host_preflight(rd,info)
    if error:stop(error)
    return serial


def require_unused_capture_epoch(rd):
    """Reject a reused capture epoch without importing packages or touching a device."""
    rd=private(rd)
    if any(os.path.lexists(rd/name) for name in CAPTURE_USED_MARKERS):
        stop('capture epoch already used')
    return rd


def flash_capture(port,bd,rd,seconds,importer=importlib.import_module):
    # This must remain before verify_device/esptool and before every operation
    # that could reset or mutate the target. Keep this resolved module for use
    # below; do not re-import after device operations.
    rd=require_unused_capture_epoch(rd)
    serial=capture_preflight(rd,importer)
    if not 35<=seconds<=120: stop('capture bound must be 35..120 seconds')
    m=require_run(rd,bd); b=backup(bd); v=validate(rd,bd); out=rd/'capture-private.txt'
    if (rd/'flash-attempt-private.json').exists() or out.exists(): stop('epoch already used')
    # Re-read complete original image now: this establishes freshness, including all NVS.
    h32.verify_device(port,b,hold=True)
    fresh=rd/'preflash-full-private.bin'
    h32.esptool(port,['--after','no_reset','read_flash','0',hex(h32.FLASH),str(fresh)],1200)
    if fresh.stat().st_size!=h32.FLASH or digest(fresh)!=b['full']['sha256']: stop('connected flash differs from original backup')
    app=Path(v['images']['app']['path'])
    app_off=v['images']['app']['offset']
    atomic_json(rd/'flash-attempt-private.json',{'epoch':m['epoch'],'app_sha256':digest(app),'offset':app_off,'bytes':app.stat().st_size,'removable_writes':'none','status':'attempted'},0o600)
    failure=None
    try:
        h32.esptool(port,['--after','no_reset','write_flash','--flash_size','16MB',hex(app_off),str(app)])
        h32.match_read(port,app_off,app,rd/'app-readback-private.bin')
        # Verify every byte outside sector-rounded application erase/write range.
        check=rd/'postflash-full-private.bin'
        h32.esptool(port,['--after','no_reset','read_flash','0',hex(h32.FLASH),str(check)],1200)
        original=fresh.read_bytes(); changed=check.read_bytes(); end=app_off+((app.stat().st_size+4095)//4096)*4096
        if len(changed)!=h32.FLASH or changed[:app_off]!=original[:app_off] or changed[end:]!=original[end:]: stop('flash changed bytes outside application erase sectors')
        h32.verify_device(port,b,hold=True)
        ser=serial.Serial(port=None,baudrate=115200,timeout=.2); ser.dtr=False; ser.rts=False; ser.port=port
        with ser, out.open('xb') as f:
            os.chmod(out,0o600); ser.reset_input_buffer()
            # Listener is open before leaving loader; toggle RTS after writing launch command.
            atomic_json(rd/'capture-ready-private.json',{'epoch':m['epoch'],'ready':True,'launch':'rts_reset','app_readback_sha256':digest(rd/'app-readback-private.bin')},0o600)
            ser.rts=True;time.sleep(.05);ser.rts=False
            deadline=time.monotonic()+seconds; pending=b''; complete=False
            while time.monotonic()<deadline:
                chunk=ser.read(4096)
                if chunk:
                    f.write(chunk); pending,lines=h32.complete_capture_lines(pending,chunk)
                    if any(b'Guru Meditation' in line or b'panic' in line.lower() for line in lines): stop('firmware panic')
                    if any(b'H35,1,COMPLETE,' in line for line in lines): complete=True;break
            f.flush();os.fsync(f.fileno())
        if not complete: stop('bounded capture ended without COMPLETE')
        parsed=parse_records(out.read_text(errors='strict'),m['epoch'],v['elf_sha256'])
        atomic_json(rd/'capture-metadata-private.json',{'epoch':m['epoch'],'status':'captured','raw_sha256':digest(out),'raw_bytes':out.stat().st_size,'manifest_sha256':digest(Path(m['build_dir'])/'manifest.json'),'original_full_sha256':b['full']['sha256'],'device_fingerprint_sha256':b['device']['fingerprint_sha256']},0o600)
    except BaseException as exc:
        failure=exc
        atomic_json(rd/'failure-private.json',{'status':'failed','error':str(exc)},0o600)
    finally:
        # H32 proven full-image restore includes independent readback, NVS/app descriptors,
        # target identity before/after and original firmware launch. Failure is never suppressed.
        h32.restore(port,bd)
        proof=json.loads((bd/'restore-proof-private.json').read_text())
        atomic_json(rd/'restore-proof-private.json',proof,0o600)
    if failure: raise failure
    print(json.dumps({'status':parsed['status'],'result':parsed['result'],'restored':'verified'}))


def aggregate(rd,bd,output):
    m=require_run(rd,bd);v=validate(rd,bd,historical=True); log=rd/'capture-private.txt'
    meta=json.loads((rd/'capture-metadata-private.json').read_text())
    proof=json.loads((rd/'restore-proof-private.json').read_text())
    if meta.get('status')!='captured' or meta['epoch']!=m['epoch'] or meta['raw_sha256']!=digest(log) or meta['raw_bytes']!=log.stat().st_size or meta['manifest_sha256']!=digest(Path(m['build_dir'])/'manifest.json') or meta['original_full_sha256']!=m['original_full_sha256'] or meta['device_fingerprint_sha256']!=m['device_fingerprint_sha256']: stop('capture metadata binding differs')
    if proof.get('status')!='verified' or proof['full_sha256']!=m['original_full_sha256'] or proof['device_fingerprint_sha256']!=m['device_fingerprint_sha256']: stop('full restore proof missing')
    parsed=parse_records(log.read_text(errors='strict'),m['epoch'],v['elf_sha256'])
    # Explicit allowlist: no raw log, CID hash, device fingerprint, MAC, serial, private paths.
    safe={k:parsed[k] for k in ('status','result','scope','qualification')}
    safe.update({'epoch':m['epoch'],'app_sha256':v['images']['app']['sha256'],'elf_sha256':v['elf_sha256'],'source_sha256':v['source']['sha256'],'restored':'verified','geometry':parsed['records'].get('GEOMETRY'),'media_backup':'not_obtained','filesystem':'not_inspected','media_writes':'none'})
    if output.exists(): stop('public evidence output already exists')
    atomic_json(output,safe);print(json.dumps(safe,indent=2))


def host_checks():
    serial,serial_info,error=resolve_serial()
    if error:stop('actual interpreter pyserial preflight failed: '+error)
    fail_closed=[]
    class MissingApiSerial:
        def __init__(self,port=None,baudrate=0,timeout=None):
            self.is_open=False;self.port=port;self.dtr=False;self.rts=False
        def read(self,n):return b''
        def close(self):pass
        def __enter__(self):return self
        def __exit__(self,*args):self.close()
    bad_api=type('SerialModule',(),{'__file__':serial.__file__,'Serial':MissingApiSerial})()
    def missing_import(name):
        imports.append(name);raise ModuleNotFoundError("No module named 'serial'")
    def missing_api(name):
        imports.append(name);return bad_api
    cases=[('missing_serial',missing_import,'serial_import_failed','serial import failed'),
           ('missing_required_api',missing_api,'pyserial_distribution_or_api_invalid',
            'pyserial distribution or required API invalid')]
    callback_names=(('require_run',globals()),('backup',globals()),('validate',globals()),
                    ('verify_device',vars(h32)),('esptool',vars(h32)),('run',vars(h32)),
                    ('match_read',vars(h32)),('restore',vars(h32)))
    for label,importer,expected_reason,expected_error in cases:
        calls=[]; imports=[]; originals=[]
        def sentinel(*args,**kwargs):
            calls.append(1);raise AssertionError('device callback invoked during preflight')
        try:
            for name,scope in callback_names:
                originals.append((scope,name,scope[name]));scope[name]=sentinel
            with tempfile.TemporaryDirectory(prefix='h35-preflight-') as td:
                run_dir=Path(td)/'run';run_dir.mkdir(mode=0o700)
                try:flash_capture('sentinel-port',None,run_dir,45,importer)
                except SystemExit as exc:
                    if str(exc)!='STOP: '+expected_error:stop(label+' returned unexpected failure: '+str(exc))
                else:stop(label+' preflight unexpectedly accepted')
                record=json.loads((run_dir/'host-preflight-private.json').read_text())
                if record['status']!='failed' or record['serial'].get('reason')!=expected_reason:
                    stop(label+' private failure metadata differs')
            if calls:stop(label+' invoked a device callback before failing closed')
            if imports!=['serial']:stop(label+' importer call sequence differs')
            fail_closed.append({'case':label,'status':'failed_closed','serial_import_calls':len(imports),
                                'metadata_reason':expected_reason,'device_callback_calls':len(calls)})
        finally:
            for scope,name,value in reversed(originals):scope[name]=value
    def snapshot_tree(root):
        return {str(p.relative_to(root)):('dir' if p.is_dir() else p.read_bytes())
                for p in sorted(root.rglob('*'))}
    for marker in CAPTURE_USED_MARKERS:
        calls=[];imports=[];originals=[]
        def sentinel(*args,**kwargs):
            calls.append(1);raise AssertionError('device callback invoked for used epoch')
        def should_not_import(name):
            imports.append(name);raise AssertionError('serial import attempted for used epoch')
        try:
            for name,scope in callback_names:
                originals.append((scope,name,scope[name]));scope[name]=sentinel
            with tempfile.TemporaryDirectory(prefix='h35-used-epoch-') as td:
                run_dir=Path(td)/'run';run_dir.mkdir(mode=0o700)
                (run_dir/marker).write_bytes(('existing:'+marker).encode())
                before=snapshot_tree(run_dir)
                try:flash_capture('sentinel-port',None,run_dir,45,should_not_import)
                except SystemExit as exc:
                    if str(exc)!='STOP: capture epoch already used':stop('used marker returned unrelated failure: '+marker)
                else:stop('used marker accepted: '+marker)
                after=snapshot_tree(run_dir)
                if before!=after:stop('used marker changed run directory: '+marker)
            if calls or imports:stop('used marker reached importer/device callback: '+marker)
            fail_closed.append({'case':'used_'+marker,'status':'refused_unchanged',
                                'serial_import_calls':len(imports),'device_callback_calls':len(calls)})
        finally:
            for scope,name,value in reversed(originals):scope[name]=value
    with tempfile.TemporaryDirectory(prefix='h35-unused-epoch-') as td:
        run_dir=Path(td)/'run';run_dir.mkdir(mode=0o700)
        calls=[];originals=[]
        def sentinel(*args,**kwargs):
            calls.append(1);raise AssertionError('device callback invoked by host preflight')
        try:
            for name,scope in callback_names:
                originals.append((scope,name,scope[name]));scope[name]=sentinel
            guarded=require_unused_capture_epoch(run_dir)
            if guarded!=run_dir.resolve():stop('unused guard did not return resolved run directory')
            resolved=capture_preflight(guarded)
            if not callable(getattr(resolved,'Serial',None)):stop('positive preflight did not return resolved pyserial module')
            record_path=run_dir/'host-preflight-private.json'
            record=json.loads(record_path.read_text())
            if record.get('status')!='pass' or (record_path.stat().st_mode & 0o777)!=0o600:
                stop('positive preflight record missing or not private')
            if calls:stop('positive host preflight invoked a device callback')
            fail_closed.append({'case':'unused_epoch_positive','status':'pass','device_callback_calls':len(calls)})
        finally:
            for scope,name,value in reversed(originals):scope[name]=value
    epoch='a'*32;elf='b'*64
    records=[('BOOT',{'reset_reason':'1','elf_sha256':elf}),('TRANSPORT',{'backend':'sdmmc','mode':'read_only','console':'usb_serial_jtag','usb_host':'disabled','slot':'0','width_requested':'4','max_freq_khz':'20000','command_timeout_ms':'1000','deadline_ms':'30000'}),('POWER',{'gpio':'43','active_level':'0','error':'0'}),('HOST',{'error':'0'}),('SLOT',{'error':'0'}),('CARD',{'error':'263'}),('STOP',{'reason':'finished','error':'263','deinit_error':'0','power_off_error':'0'}),('COMPLETE',{'result':'init_failed','card_present':'unproven','scope':'discovery_only'})]
    def render(rows): return '\n'.join('H35,1,'+e+',epoch='+epoch+','+','.join(k+'='+v for k,v in x.items()) for e,x in rows)
    valid=render(records);parse_records(valid,epoch,elf)
    success=records[:5]+[('CARD',{'error':'0'}),('GEOMETRY',{'sectors':'1000','sector_bytes':'512','capacity_bytes':'512000','bus_width':'4','real_freq_khz':'20000','card_max_freq_khz':'25000','ddr':'0'}),('IDENTITY',{'scheme':'epoch_sha256_decoded_cid_v1','sha256':'e'*64}),('STOP',{'reason':'finished','error':'0','deinit_error':'0','power_off_error':'0'}),('COMPLETE',{'result':'detected','card_present':'proven','scope':'discovery_only'})]
    parse_records(render(success),epoch,elf)
    parse_records(render(records[:2]+[('STOP',{'reason':'deadline','error':'263'}),('COMPLETE',{'result':'timeout','card_present':'unproven','scope':'discovery_only'})]),epoch,elf)
    variants={'wrong_geometry':render(success).replace('capacity_bytes=512000','capacity_bytes=500000'),'false_success':valid.replace('result=init_failed','result=detected'),'duplicate':valid+'\n'+valid.splitlines()[-1],'truncated': '\n'.join(valid.splitlines()[:-1]),'wrong_epoch':valid.replace(epoch,'c'*32),'wrong_elf':valid.replace(elf,'d'*64),'unknown':valid.replace('CARD','WRITE'),'private_field':valid.replace('error=263','error=263,serial=secret',1),'wrong_order':render(records[:2]+[records[3],records[2]]+records[4:]),'failed_prerequisite':valid.replace('H35,1,HOST,epoch='+epoch+',error=0','H35,1,HOST,epoch='+epoch+',error=1'),'false_presence':valid.replace('card_present=unproven','card_present=proven')}
    for reason,label in [('5','interrupt_watchdog'),('6','task_watchdog'),('7','other_watchdog')]:
        variants[label]=render(success).replace('reset_reason=1,','reset_reason='+reason+',')
    for name,text in variants.items():
        try:parse_records(text,epoch,elf)
        except SystemExit:pass
        else:stop('structural check accepted '+name)
    print(json.dumps({'status':'pass','synthetic_only':True,'rejected':list(variants),
                      'pyserial_preflight':{'status':'pass','distribution':serial_info['distribution'],
                                            'version':serial_info['version']},
                      'preflight_negative_cases':fail_closed}))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['inventory','prepare-run','build','validate-build','flash-capture','parse','restore','host-checks']);p.add_argument('--port');p.add_argument('--backup-dir',type=Path,default=DEFAULT_BOX_BACKUP_DIR);p.add_argument('--run-dir',type=Path);p.add_argument('--output',type=Path);p.add_argument('--seconds',type=int,default=45);p.add_argument('--set-current',action='store_true',help='after parse, write current-h35-capture.json pointer');a=p.parse_args()
    if a.command=='host-checks':host_checks();return
    if a.command=='inventory':h32.inventory(h32.one_port(a.port));return
    if a.command=='restore':
        if not a.backup_dir:p.error('--backup-dir required')
        backup(a.backup_dir);h32.restore(h32.one_port(a.port),a.backup_dir);return
    if not a.run_dir:a.run_dir=default_h35_run_dir()
    if a.command=='prepare-run':prepare(a.run_dir,a.backup_dir)
    elif a.command=='build':build(a.run_dir,a.backup_dir)
    elif a.command=='validate-build':print(json.dumps(validate(a.run_dir,a.backup_dir),indent=2))
    elif a.command=='flash-capture':flash_capture(h32.one_port(a.port),a.backup_dir,a.run_dir,a.seconds)
    elif a.command=='parse':
        if not a.output:p.error('--output required')
        aggregate(a.run_dir,a.backup_dir,a.output)
        if a.set_current:write_current_h35_capture_pointer(a.run_dir)
if __name__=='__main__':main()
