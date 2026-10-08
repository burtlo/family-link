#!/usr/bin/env python3
"""Private, fail-closed H37 read-only SD metadata classification controller.

The microSD is disposable under the dated owner authorization. This controller
never backs up or restores its contents; the BOX-3 flash image is always restored.
"""
from __future__ import annotations

import argparse, importlib, importlib.metadata, json, os, re, secrets
import subprocess, sys, tempfile, time
from pathlib import Path
from types import SimpleNamespace

import h32_storage_qual as h32
import h35_attached_discovery as h35
import h38_sd_contract as sd_contract
from attached_storage_paths import DEFAULT_BOX_BACKUP_DIR, resolve_h35_capture_dir

ROOT=h32.ROOT; FW=h32.FW
H35_EPOCH_LEGACY='a1617be8cb2d2343e744c31cc7d1b933'
H35_EPOCH=H35_EPOCH_LEGACY  # backward compat for imports expecting a module constant
H35_BACKUP=DEFAULT_BOX_BACKUP_DIR
_CLI_H35_CAPTURE: Path | None = None
SECTOR_BYTES=512; CAP=131_072; RAW_MAX=1<<20
RAW_MARKERS=('flash-attempt-private.json','capture-private.jsonl','capture-raw-private.bin','host-preflight-private.json','capture-ready-private.json','read-plan-private.json','dispatch-ledger-private.json','sector-snapshot-private.bin.json','capture-metadata-private.json','restore-proof-private.json','current-device-proof-private.json')

stop=h32.stop; digest=h32.digest; atomic_json=h32.atomic_json

def private(path):
    if path is None or path.resolve().is_relative_to(ROOT.resolve()) or path.is_symlink():
        stop('private directory outside repository required')
    if path.exists() and path.stat().st_mode & 0o077: stop('private directory permissions must be 0700')
    return path.resolve()

def backup(path):
    b=h32.require_backup(path)
    if b['full']['bytes']!=h32.FLASH or b['device'].get('chip')!='esp32-s3' or b['device'].get('flash_bytes')!=h32.FLASH:
        stop('backup must be complete ESP32-S3 16 MiB image')
    return b

def source_identity():
    inputs=[FW/'CMakeLists.txt',FW/'main/CMakeLists.txt',FW/'main/idf_component.yml',FW/'dependencies.lock',FW/'sdkconfig.defaults',FW/'sdkconfig.h37.defaults',FW/'partitions/h37_sdmmc_classification.csv',FW/'demos/h37_sdmmc_classification.c',Path(__file__).resolve(),ROOT/'scripts/h32_storage_qual.py',ROOT/'scripts/h35_attached_discovery.py',ROOT/'scripts/h37_sd_metadata.py',ROOT/'scripts/h37_sd_metadata_checks.py']
    missing=[p for p in inputs if not p.is_file()]
    if missing:stop('required H37 source missing: '+', '.join(str(p.relative_to(ROOT)) for p in missing))
    inputs += sorted((FW/'managed_components/espressif__esp-box-3').rglob('*.h'))
    inputs += sorted((FW/'managed_components/espressif__esp-box-3').rglob('*.c'))
    inputs += sorted((FW/'main').glob('*.c'))+sorted((FW/'main').glob('*.h'))
    files={str(p.relative_to(ROOT)):digest(p) for p in inputs if p.is_file()}
    return {'files':files,'sha256':h32.dh(json.dumps(files,sort_keys=True).encode())}

def sdk_identity():
    files=[h32.IDF/'tools/cmake/version.cmake',h32.IDF/'components/esp_system/include/esp_system.h',
           h32.IDF/'components/sdmmc/sdmmc_cmd.c',h32.IDF/'components/sdmmc/sdmmc_init.c',
           h32.IDF/'components/esp_driver_sdmmc/src/sdmmc_host.c',
           h32.IDF/'components/esp_driver_usb_serial_jtag/src/usb_serial_jtag.c',
           h32.IDF/'components/esp_driver_usb_serial_jtag/src/usb_serial_jtag_vfs.c',
           h32.IDF/'components/esp_app_format/include/esp_app_desc.h']
    missing=[p for p in files if not p.is_file()]
    if missing:stop('pinned IDF source unavailable: '+', '.join(str(p) for p in missing))
    return {str(p.relative_to(h32.IDF)):digest(p) for p in files}

def tool_versions():
    esptool=h32.run(['python','-m','esptool','version'],capture=True).stdout.strip()
    compiler=h32.run(['xtensa-esp32s3-elf-gcc','--version'],capture=True).stdout.splitlines()[0].strip()
    return {'idf':h32.idf_version(),'python':sys.version,'esptool':esptool,'compiler':compiler}

def require_run(rd,bd=None):
    rd=private(rd)
    try:m=json.loads((rd/'run-private.json').read_text())
    except Exception as e:stop(f'invalid H37 run metadata: {e}')
    if m.get('schema')!=1 or not h32.EPOCH_RE.fullmatch(m.get('epoch','')) or m.get('mode')!='sdmmc_classification':stop('invalid H37 epoch')
    if Path(m['build_dir']).resolve()!=rd/'build':stop('build directory differs from private epoch')
    if bd is not None:
        b=backup(bd)
        if m.get('original_full_sha256')!=b['full']['sha256'] or m.get('device_fingerprint_sha256')!=b['device']['fingerprint_sha256']:stop('backup binding differs')
    return m

def set_h35_capture_dir_cli(path: Path | None) -> None:
    global _CLI_H35_CAPTURE
    _CLI_H35_CAPTURE = private(path) if path is not None else None

def h35_capture_dir(capture_dir: Path | None = None, run_meta: dict | None = None) -> Path:
    if capture_dir is not None:
        return private(capture_dir)
    if run_meta and run_meta.get('h35_capture_private'):
        return private(Path(run_meta['h35_capture_private']))
    if _CLI_H35_CAPTURE is not None:
        return _CLI_H35_CAPTURE
    try:
        return private(resolve_h35_capture_dir())
    except ValueError as exc:
        stop(str(exc))

def prepare(rd,bd,h35_capture_dir_arg: Path | None = None):
    rd=private(rd);b=backup(bd)
    if rd.exists():stop('immutable run directory already exists')
    cap=h35_capture_dir(h35_capture_dir_arg)
    h35_epoch,_=read_h35_reference(capture_dir=cap)
    rd.mkdir(parents=True,mode=0o700);rd.chmod(0o700)
    m={'schema':1,'epoch':secrets.token_hex(16),'mode':'sdmmc_classification','created_unix':int(time.time()),'build_dir':str(rd/'build'),'backup_dir':str(bd.resolve()),'original_full_sha256':b['full']['sha256'],'device_fingerprint_sha256':b['device']['fingerprint_sha256'],'h35_reference_epoch':h35_epoch,'h35_capture_private':str(cap),'microSD_backup':'waived_by_owner_2026-10-05'}
    atomic_json(rd/'run-private.json',m,0o600)
    print(json.dumps({'status':'prepared','epoch':m['epoch'],'h35_reference_epoch':h35_epoch,'h35_capture_private':str(cap)}))

def build(rd,bd):
    m=require_run(rd,bd);b=backup(bd);d=Path(m['build_dir'])
    if d.exists():stop('build directory already exists; prepare a new epoch')
    before=source_identity();d.mkdir(mode=0o700)
    ps=b['partition_table']['entries'];factory=[p for p in ps if p['name']=='factory' and p['type']==0 and p['subtype']==0 and p['offset']==h32.APP_OFF]
    if len(factory)!=1:stop('original factory slot required')
    csv=d/'original-partitions.csv';csv.write_text('# Original private backup partition map\n'+''.join(f"{p['name']},0x{p['type']:x},0x{p['subtype']:x},0x{p['offset']:x},0x{p['size']:x},{'encrypted' if p['flags']==1 else ''}\n" for p in ps))
    defaults=d/'partition.defaults';defaults.write_text(f'CONFIG_PARTITION_TABLE_CUSTOM=y\nCONFIG_PARTITION_TABLE_CUSTOM_FILENAME="{csv}"\nCONFIG_PARTITION_TABLE_OFFSET=0x8000\n')
    h35_ref=m.get('h35_reference_epoch',H35_EPOCH_LEGACY)
    h32.run(['idf.py','-D','FAMILY_DEMO=h37_sdmmc_classification','-D',f"H37_RUN_EPOCH={m['epoch']}",'-D',f"H35_RUN_EPOCH={h35_ref}",'-D',f'SDKCONFIG={d}/sdkconfig','-D',f'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.h37.defaults;{defaults}','-B',str(d),'-C',str(FW),'build'])
    if before!=source_identity():stop('source changed during compilation')
    snap=d/'source-snapshot'
    for rel,sha in before['files'].items():
        p=snap/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/rel).read_bytes())
        if digest(p)!=sha:stop('source snapshot changed')
    images={n:{'path':str(p),'bytes':p.stat().st_size,'sha256':digest(p),'offset':off} for n,p,off in [('app',d/'family_link_demo.bin',h32.APP_OFF),('partition_table',d/'partition_table/partition-table.bin',h32.PT_OFF),('bootloader',d/'bootloader/bootloader.bin',0)]}
    manifest={'schema':1,'epoch':m['epoch'],'source':before,'sdk':sdk_identity(),'tools':tool_versions(),'git_commit':h32.git_head(),'sdkconfig_sha256':digest(d/'sdkconfig'),'partition_csv_sha256':digest(csv),'partitions':h32.partitions(Path(images['partition_table']['path']).read_bytes()),'images':images,'elf_sha256':digest(d/'family_link_demo.elf'),'app_descriptor':h32.app_desc(Path(images['app']['path']).read_bytes()),'factory':factory[0],'h35_reference_epoch':h35_ref}
    atomic_json(d/'manifest.json',manifest,0o600);validate(rd,bd)

def validate(rd,bd,historical=False):
    m=require_run(rd,bd);b=backup(bd);d=Path(m['build_dir']);v=json.loads((d/'manifest.json').read_text())
    h35_ref=m.get('h35_reference_epoch',H35_EPOCH_LEGACY)
    if v.get('schema')!=1 or v.get('epoch')!=m['epoch'] or v.get('h35_reference_epoch')!=h35_ref:stop('manifest epoch/schema/reference differs')
    bound=Path(m.get('h35_capture_private',''))
    if bound and h35_capture_dir(run_meta=m).resolve()!=bound.resolve():stop('H35 capture directory binding differs')
    if tool_versions()!=v.get('tools'):stop('effective build tool versions differ')
    for rel,sha in v.get('sdk',{}).items():
        if not (h32.IDF/rel).is_file() or digest(h32.IDF/rel)!=sha:stop('pinned SDK source mismatch: '+rel)
    if not historical and v['source']!=source_identity():stop('source changed; new epoch required')
    for rel,sha in v['source']['files'].items():
        if digest(d/'source-snapshot'/rel)!=sha:stop('source snapshot mismatch')
    if digest(d/'sdkconfig')!=v['sdkconfig_sha256'] or digest(d/'original-partitions.csv')!=v['partition_csv_sha256']:stop('config mismatch')
    for x in v['images'].values():
        p=Path(x['path'])
        if not p.resolve().is_relative_to(d.resolve()) or p.stat().st_size!=x['bytes'] or digest(p)!=x['sha256']:stop('image binding mismatch')
    ps=h32.partitions(Path(v['images']['partition_table']['path']).read_bytes())
    if ps!=b['partition_table']['entries'] or ps!=v['partitions']:stop('partition layout differs from original')
    f=v['factory'];app=v['images']['app'];
    if f['offset']!=h32.APP_OFF or app['offset']!=f['offset'] or app['bytes']>f['size']*85//100:stop('factory bounds/headroom invalid')
    if digest(d/'family_link_demo.elf')!=v['elf_sha256'] or h32.app_desc(Path(app['path']).read_bytes())!=v['app_descriptor'] or v['app_descriptor']['elf_sha256']!=v['elf_sha256']:stop('ELF descriptor binding differs')
    cfg=(d/'sdkconfig').read_text()
    if 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y' not in cfg or 'CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y' not in cfg:stop('required native USB/16MB configuration missing')
    return v

def resolve_serial(importer=importlib.import_module):
    # Deliberately local: BOX emergency restore must not depend on pyserial.
    return h35.resolve_serial(importer)

def capture_preflight(rd,importer=importlib.import_module):
    serial,info,error=resolve_serial(importer)
    if not error:
        try:
            ctor=serial.Serial;port=ctor(port=None,baudrate=115200,timeout=.2,write_timeout=.5)
            try:
                if not callable(getattr(port,'write',None)):raise ValueError('Serial.write missing')
                if getattr(port,'write_timeout',None)!=.5:raise ValueError('bounded Serial.write_timeout missing')
                info=dict(info);info['api']=list(info.get('api',[]))+['write','write_timeout=0.5']
            finally:port.close()
        except Exception:
            serial=None;info={'status':'failed','reason':'pyserial_write_api_invalid'};error='pyserial write API invalid'
    h35._write_host_preflight(rd,info)
    if error:stop(error)
    return serial

def read_h35_identity(capture_dir: Path | None = None, run_meta: dict | None = None):
    # H35 identity is private evidence and must be read from that immutable capture.
    cap=h35_capture_dir(capture_dir,run_meta)
    p=cap/'capture-private.txt'
    if not p.is_file():stop('private successful H35 capture unavailable')
    side=cap/'capture-metadata-private.json'
    try: metadata=json.loads(side.read_text())
    except Exception as e:stop(f'private H35 capture metadata unavailable: {e}')
    h35_epoch=metadata.get('epoch','')
    if not h32.EPOCH_RE.fullmatch(h35_epoch):stop('H35 capture metadata epoch invalid')
    if metadata.get('status')!='captured' or metadata.get('raw_sha256')!=digest(p) or metadata.get('raw_bytes')!=p.stat().st_size:stop('H35 capture metadata binding invalid')
    build_manifest=Path(metadata.get('manifest_path',''))
    if not build_manifest.is_file():
        # H35 capture metadata predates this optional path field; resolve only
        # from its bound private epoch directory, never from public evidence.
        runmeta=cap/'run-private.json'
        try: run=json.loads(runmeta.read_text())
        except Exception as e:stop(f'private H35 run manifest unavailable: {e}')
        build_manifest=Path(run.get('build_dir',''))/'manifest.json'
    try: manifest=json.loads(build_manifest.read_text());elf=manifest['elf_sha256']
    except Exception as e:stop(f'private H35 build manifest invalid: {e}')
    if metadata.get('manifest_sha256')!=digest(build_manifest):stop('H35 build manifest hash differs from capture metadata')
    if manifest.get('epoch')!=h35_epoch or not re.fullmatch('[0-9a-f]{64}',elf):stop('H35 build manifest epoch/ELF invalid')
    parsed=h35.parse_records(p.read_text(errors='strict'),h35_epoch,elf)
    ident=parsed['records'].get('IDENTITY',{})
    if ident.get('scheme')!='epoch_sha256_decoded_cid_v1' or not re.fullmatch('[0-9a-f]{64}',ident.get('sha256','')):stop('H35 private identity reference invalid')
    if parsed.get('result')!='detected':stop('H35 reference capture did not prove detected card')
    g=parsed['records'].get('GEOMETRY',{})
    try: geometry={k:int(g[k]) for k in ('sectors','sector_bytes','capacity_bytes','bus_width','real_freq_khz','ddr')}
    except (KeyError,ValueError):stop('H35 geometry reference invalid')
    require_card_geometry(geometry)
    return ident['sha256']

def read_h35_reference(capture_dir: Path | None = None, run_meta: dict | None = None):
    """Return (reference_epoch, private_cid_sha256) from the bound H35 capture."""
    cap=h35_capture_dir(capture_dir,run_meta)
    cid=read_h35_identity(capture_dir=cap)
    metadata=json.loads((cap/'capture-metadata-private.json').read_text())
    return metadata['epoch'], cid

def require_card_geometry(g):
    try:
        sectors=int(g['sectors']); sector_bytes=int(g['sector_bytes']); capacity=int(g['capacity_bytes'])
        bus_width=int(g['bus_width']); freq=int(g['real_freq_khz']); ddr=int(g['ddr'])
    except (KeyError,TypeError,ValueError):
        stop('card geometry fields invalid')
    sd_contract.validate_card_sector_count(sectors)
    if sector_bytes!=SECTOR_BYTES or capacity!=sectors*SECTOR_BYTES or bus_width!=4 or freq!=20000 or ddr!=0:
        stop('card geometry/bus fields differ from frozen SDMMC profile')

def require_h37_geometry(g):
    require_card_geometry(g)

def card_geometry_from_run(rd):
    rd=private(rd)
    m=json.loads((rd/'run-private.json').read_text())
    if 'card_geometry' in m:
        return {k:int(m['card_geometry'][k]) for k in ('sectors','sector_bytes','capacity_bytes','bus_width','real_freq_khz','ddr')}
    meta=json.loads((rd/'capture-metadata-private.json').read_text())
    if meta.get('status')!='captured' or meta.get('epoch')!=m['epoch']:
        stop('H37 capture metadata missing for geometry')
    raw=(rd/'capture-raw-private.bin').read_bytes()
    v=validate(rd,Path(m['backup_dir']),historical=True)
    plan=json.loads((rd/'read-plan-private.json').read_text())
    ledger=json.loads((rd/'dispatch-ledger-private.json').read_text())
    plan_rows=[x for stage in plan['stages'] for x in stage]
    planned=[(int(x['lba']),int(x['count'])) for x in plan_rows]
    dispatched=[(int(x['lba']),int(x['count'])) for x in ledger['dispatches']]
    md=importlib.import_module('h37_sd_metadata')
    parsed=md.parse_capture(raw,m['epoch'],v['elf_sha256'],m.get('h35_reference_epoch',H35_EPOCH),
                            read_h35_identity(),planned,dispatched)
    for rec in parsed.rows:
        if rec.event=='GEOMETRY':
            return {k:int(rec.fields[k]) for k in ('sectors','sector_bytes','capacity_bytes','bus_width','real_freq_khz','ddr')}
    stop('H37 capture lacks GEOMETRY record')

def require_unused_capture_epoch(rd):
    rd=private(rd)
    if any(os.path.lexists(rd/name) for name in RAW_MARKERS):stop('H37 capture epoch already used')
    return rd

def _line_read(ser,pending,deadline,stall_deadline,rd,elf,epoch,parser,raw_file):
    """Read one bounded complete firmware record; parser is installed by metadata module."""
    while time.monotonic()<deadline and time.monotonic()<stall_deadline:
        if b'\n' in pending:
            line,pending=pending.split(b'\n',1);line+=b'\n'
            if len(line)>8192:stop('firmware record exceeds 8192-byte limit')
            if line.startswith(b'H37C,'):continue
            if not line.startswith(b'H37,'):
                if parser.records:stop('unexpected non-H37 output after firmware BOOT')
                continue
            try:return line.decode('ascii','strict'),pending
            except UnicodeDecodeError:stop('non-ASCII firmware record')
        data=ser.read(512)
        if not data:continue
        if raw_file.tell()+len(data)>RAW_MAX:stop('raw capture exceeds 1 MiB')
        raw_file.write(data);raw_file.flush()
        pending+=data
        if len(pending)>8192 and b'\n' not in pending:stop('firmware record exceeds 8192-byte limit')
        if b'\n' not in pending:continue
        line,pending=pending.split(b'\n',1)
        line+=b'\n'
        if len(line)>8192:stop('firmware record exceeds 8192-byte limit')
        if line.startswith(b'H37C,'):continue
        if not line.startswith(b'H37,'):
            if parser.records:stop('unexpected non-H37 output after firmware BOOT')
            continue
        try:line=line.decode('ascii','strict')
        except UnicodeDecodeError:stop('non-ASCII firmware record')
        return line,pending
    stop('firmware output timeout')

def _save_plan(rd,epoch,elf,stages):
    atomic_json(rd/'read-plan-private.json',{'schema':1,'epoch':epoch,'elf_sha256':elf,'stages':stages},0o600)

def _save_dispatches(rd,epoch,elf,dispatches):
    atomic_json(rd/'dispatch-ledger-private.json',{'schema':1,'epoch':epoch,'elf_sha256':elf,'dispatches':dispatches},0o600)

def _save_snapshot(rd,epoch,elf,sector_map):
    # This file contains raw sector bytes and is always private.
    import base64
    atomic_json(rd/'sector-snapshot-private.bin.json',{'schema':1,'epoch':epoch,'elf_sha256':elf,
        'sectors':{str(k):base64.b64encode(v).decode('ascii') for k,v in sorted(sector_map.items())}},0o600)

def drive_classification(parser,ser,rd,m,v,raw_file,transcript,metadata,geometry,pending,deadline):
    """Run validated staged reads, preserving every response and private ledger."""
    classifier=metadata.MetadataClassifier(geometry)
    stages=[];dispatches=[];seq=1;all_requests=[]
    while True:
        if time.monotonic()>=deadline:stop('classification total timeout')
        requests=classifier.next_requests()
        if not requests:break
        stage_rows=[{'lba':r.lba,'count':r.count,'stage':r.stage} for r in requests]
        stages.append(stage_rows);_save_plan(rd,m['epoch'],v['elf_sha256'],stages)
        parser.set_read_plan(requests)
        for r in requests:
            parser.mark_dispatched(r.lba,r.count)
            row={'seq':seq,'lba':r.lba,'count':r.count,'stage':r.stage,'charged_bytes':r.count*SECTOR_BYTES,
                 'epoch':m['epoch'],'elf_sha256':v['elf_sha256'],'dispatched_unix_ns':time.time_ns(),'result':'pending'}
            dispatches.append(row);_save_dispatches(rd,m['epoch'],v['elf_sha256'],dispatches)
            all_requests.append((r.lba,r.count))
            _send(ser,f"H37C,1,READ,{m['epoch']},{v['elf_sha256']},{seq},{r.lba},{r.count}\n")
            now=time.monotonic()
            if now>=deadline:stop('classification total timeout')
            line,pending=_line_read(ser,pending,min(deadline,now+5),min(deadline,now+15),rd,v['elf_sha256'],m['epoch'],parser,raw_file)
            transcript.write(line.encode('ascii'));transcript.flush()
            record=parser.feed_line(line)
            if record.event!='READ_RESULT':stop('expected READ_RESULT after dispatch')
            row.update({'result':'returned' if record.fields['error']=='0' else 'error',
                        'error':record.fields['error'],'elapsed_us':record.fields['elapsed_us'],
                        'returned_bytes':record.fields['len'],'response_seq':record.fields['seq']})
            _save_dispatches(rd,m['epoch'],v['elf_sha256'],dispatches)
            if record.fields['error']!='0':return classifier,None,stages,dispatches,all_requests,pending
            classifier.accept_record(record)
            _save_snapshot(rd,m['epoch'],v['elf_sha256'],classifier.sector_map)
            seq+=1
    result=classifier.result()
    _save_plan(rd,m['epoch'],v['elf_sha256'],stages)
    # Private reclassification inputs bind each extent to the staged plan and capture.
    _save_dispatches(rd,m['epoch'],v['elf_sha256'],dispatches)
    if time.monotonic()>=deadline:stop('classification total timeout')
    parser.set_read_plan([])
    _send(ser,f"H37C,1,FINISH,{m['epoch']},{v['elf_sha256']},{seq}\n")
    return classifier,result,stages,dispatches,all_requests,pending

def drain_failure_terminal(parser,ser,rd,elf,epoch,raw_file,transcript,pending,deadline):
    """Collect a bounded firmware failure terminal after a host-side failure."""
    end=min(deadline,time.monotonic()+20)
    while time.monotonic()<end:
        try:
            line,pending=_line_read(ser,pending,end,min(end,time.monotonic()+5),rd,elf,epoch,parser,raw_file)
        except BaseException:
            return False,pending
        transcript.write(line.encode('ascii'));transcript.flush()
        try:record=parser.feed_line(line)
        except Exception:
            # Preserve malformed lines privately, but never turn them into evidence.
            continue
        if record.event=='COMPLETE':return True,pending
    return False,pending

def replay_classification(capture,run_dir,epoch,elf):
    """Re-derive extents/verdict from strictly parsed READ_RESULT bytes."""
    import base64
    plan=json.loads((run_dir/'read-plan-private.json').read_text())
    ledger=json.loads((run_dir/'dispatch-ledger-private.json').read_text())
    if plan.get('schema')!=1 or plan.get('epoch')!=epoch or plan.get('elf_sha256')!=elf or ledger.get('schema')!=1 or ledger.get('epoch')!=epoch or ledger.get('elf_sha256')!=elf:
        stop('private plan/dispatch binding differs')
    if [ (x['lba'],x['count']) for stage in plan['stages'] for x in stage ] != [ (x['lba'],x['count']) for x in ledger['dispatches'] ]:
        stop('dispatch ledger differs from staged plan')
    records=capture.records;geometry=None
    for rec in records:
        if rec.event=='GEOMETRY':geometry={k:int(v) for k,v in rec.fields.items()}
    if geometry is None:stop('capture lacks geometry')
    md=importlib.import_module('h37_sd_metadata');classifier=md.MetadataClassifier(geometry)
    expected=classifier.next_requests();stage_index=0;stage_pos=0;read_idx=0
    def verify_stage(reqs,index):
        if index>=len(plan['stages']):
            if reqs:stop('replayed classifier produced an unpersisted stage')
            return
        got=[{'lba':r.lba,'count':r.count,'stage':r.stage} for r in reqs]
        if got!=plan['stages'][index]:stop('persisted staged plan differs from classifier derivation')
    verify_stage(expected,stage_index)
    for rec in records:
        if rec.event!='READ_RESULT':continue
        if not expected or read_idx>=len(ledger['dispatches']):stop('unexpected READ_RESULT during replay')
        wanted=expected[stage_pos];row=ledger['dispatches'][read_idx]
        if (wanted.lba,wanted.count)!=(int(rec.fields['lba']),int(rec.fields['count'])) or (row['seq']!=int(rec.fields['seq']) or row['stage']!=wanted.stage):stop('raw read differs from replayed classifier plan')
        expected_result='returned' if rec.fields['error']=='0' else 'error'
        if row.get('result')!=expected_result or row.get('error')!=rec.fields['error'] or row.get('returned_bytes')!=rec.fields['len'] or row.get('elapsed_us')!=rec.fields['elapsed_us'] or row.get('response_seq')!=rec.fields['seq']:
            stop('private dispatch ledger differs from raw READ_RESULT')
        classifier.accept_record(rec);stage_pos+=1;read_idx+=1
        if stage_pos==len(expected):
            expected=classifier.next_requests();stage_index+=1;stage_pos=0;verify_stage(expected,stage_index)
    if capture.terminal.get('result')!='read_complete':stop('failed capture cannot produce a classification')
    if expected or stage_index!=len(plan['stages']) or read_idx!=len(ledger['dispatches']):stop('replayed classifier plan is incomplete')
    result=classifier.result()
    if not result.planned_reads or result.read_bytes!=capture.charged_bytes:stop('classification coverage differs from terminal ledger')
    # Ensure raw sector snapshot is exactly the parser-accepted result bytes.
    snap=json.loads((run_dir/'sector-snapshot-private.bin.json').read_text())
    raw={int(k):base64.b64decode(v,validate=True) for k,v in snap['sectors'].items()}
    if snap.get('schema')!=1 or snap.get('epoch')!=epoch or snap.get('elf_sha256')!=elf or raw!=dict(classifier.sector_map):stop('private sector snapshot differs from raw capture')
    return result,geometry,plan,ledger

def _send(ser,text):
    data=text.encode('ascii')
    if len(data)>256 or not data.endswith(b'\n'):stop('host command exceeds frozen bound')
    n=ser.write(data)
    if n!=len(data):stop('short command write')

def flash_capture(port,bd,rd,importer=importlib.import_module):
    rd=require_unused_capture_epoch(rd)
    serial=capture_preflight(rd,importer)
    m=require_run(rd,bd);b=backup(bd);v=validate(rd,bd);out=rd/'capture-private.jsonl'
    h35_ref=m.get('h35_reference_epoch',H35_EPOCH_LEGACY)
    ref=read_h35_identity(run_meta=m)
    meta=importlib.import_module('h37_sd_metadata')
    parser=meta.ProtocolParser(m['epoch'],v['elf_sha256'],h35_ref,ref)
    live_device=h32.verify_device(port,b,hold=True)
    proof_device={k:live_device[k] for k in ('chip','chip_description','flash_manufacturer','flash_device','flash_bytes','fingerprint_sha256')}
    proof={'schema':1,'status':'verified','epoch':m['epoch'],'source_sha256':v['source']['sha256'],
        'elf_sha256':v['elf_sha256'],'backup_device_fingerprint_sha256':b['device']['fingerprint_sha256'],
        'live_device':proof_device,'matches_backup':live_device['fingerprint_sha256']==b['device']['fingerprint_sha256'],
        'reset_held':True}
    if not proof['matches_backup']:stop('current device proof differs from original backup')
    atomic_json(rd/'current-device-proof-private.json',proof,0o600)
    fresh=rd/'preflash-full-private.bin';h32.esptool(port,['--after','no_reset','read_flash','0',hex(h32.FLASH),str(fresh)],1200)
    if fresh.stat().st_size!=h32.FLASH or digest(fresh)!=b['full']['sha256']:stop('connected flash differs from original backup')
    app=Path(v['images']['app']['path']);atomic_json(rd/'flash-attempt-private.json',{'epoch':m['epoch'],'app_sha256':digest(app),'offset':h32.APP_OFF,'bytes':app.stat().st_size,'status':'attempted'},0o600)
    failure=None
    try:
        h32.esptool(port,['--after','no_reset','write_flash','--flash_size','16MB',hex(h32.APP_OFF),str(app)])
        h32.match_read(port,h32.APP_OFF,app,rd/'app-readback-private.bin')
        post=rd/'postflash-full-private.bin';h32.esptool(port,['--after','no_reset','read_flash','0',hex(h32.FLASH),str(post)],1200)
        old=fresh.read_bytes();new=post.read_bytes();end=h32.APP_OFF+((app.stat().st_size+4095)//4096)*4096
        if len(new)!=h32.FLASH or new[:h32.APP_OFF]!=old[:h32.APP_OFF] or new[end:]!=old[end:]:stop('flash changed bytes outside app erase sectors')
        h32.verify_device(port,b,hold=True)
        raw_path=rd/'capture-raw-private.bin'
        if raw_path.exists():stop('private raw capture already exists')
        ser=serial.Serial(port=None,baudrate=115200,timeout=.05,write_timeout=.5);ser.dtr=False;ser.rts=False;ser.port=port
        with raw_path.open('xb') as raw_file,out.open('xb') as transcript:
            os.chmod(raw_path,0o600);os.chmod(out,0o600)
            _save_plan(rd,m['epoch'],v['elf_sha256'],[]);_save_dispatches(rd,m['epoch'],v['elf_sha256'],[])
            with ser:
                ser.reset_input_buffer()
                atomic_json(rd/'capture-ready-private.json',{'epoch':m['epoch'],'ready':True,'listener_first':True},0o600)
                ser.rts=True;time.sleep(.05);ser.rts=False
                deadline=time.monotonic()+240;discovery_deadline=time.monotonic()+45;pending=b'';complete=False;classified=None;geometry=None
                stream_failure=None
                try:
                    while time.monotonic()<deadline:
                        if not parser.ready and time.monotonic()>discovery_deadline:stop('transport discovery timeout')
                        now=time.monotonic();active_deadline=deadline if parser.ready else min(deadline,discovery_deadline)
                        record,pending=_line_read(ser,pending,active_deadline,min(active_deadline,now+15),rd,v['elf_sha256'],m['epoch'],parser,raw_file)
                        transcript.write(record.encode('ascii'));transcript.flush()
                        event=parser.feed_line(record)
                        if event.event=='GEOMETRY':
                            geometry={k:int(x) for k,x in event.fields.items()};require_h37_geometry(geometry)
                        elif event.event=='READY':
                            if parser.cid_digest_matches is not True:stop('fresh H37 CID does not match private H35 identity')
                            if geometry is None:stop('READY arrived without validated geometry')
                            _send(ser,f"H37C,1,BIND,{m['epoch']},{v['elf_sha256']},{h35_ref},{ref}\n")
                        elif event.event=='IDENTITY_MATCH':
                            if event.fields.get('match')!='1' or event.fields.get('error')!='0':stop('private H35 identity match failed')
                            if geometry is None:stop('identity match arrived without geometry')
                            classified=drive_classification(parser,ser,rd,m,v,raw_file,transcript,meta,geometry,pending,deadline)
                            pending=classified[5]
                        elif event.event=='COMPLETE':complete=True;break
                except BaseException as exc:
                    stream_failure=exc
                    if parser.records and parser.terminal is None:
                        complete,pending=drain_failure_terminal(parser,ser,rd,v['elf_sha256'],m['epoch'],raw_file,transcript,pending,deadline)
                    if not complete:raise
                if not complete:stop('bounded capture ended without COMPLETE')
            raw_file.flush();os.fsync(raw_file.fileno());transcript.flush();os.fsync(transcript.fileno())
            if not complete:stop('bounded capture ended without COMPLETE')
        dispatch_ledger=json.loads((rd/'dispatch-ledger-private.json').read_text())
        dispatches=[(x['lba'],x['count']) for x in dispatch_ledger['dispatches']]
        parsed=meta.parse_capture(raw_path.read_bytes(),m['epoch'],v['elf_sha256'],h35_ref,ref,
            [(x['lba'],x['count']) for stage in json.loads((rd/'read-plan-private.json').read_text())['stages'] for x in stage],dispatches)
        if parsed.terminal.get('result')!='read_complete':stop('firmware emitted failed terminal: '+parsed.terminal.get('failure_stage','unknown'))
        if stream_failure is not None:raise stream_failure
        if classified is None or classified[1] is None:stop('failed sector read cannot produce a classification')
        replayed,geometry,_,_=replay_classification(parsed,rd,m['epoch'],v['elf_sha256'])
        if replayed!=classified[1]:stop('live and offline classification results differ')
        geom={k:int(v) for k,v in geometry.items()}
        require_card_geometry(geom)
        m['card_geometry']=geom
        atomic_json(rd/'run-private.json',m,0o600)
        atomic_json(rd/'capture-metadata-private.json',{'epoch':m['epoch'],'status':'captured','raw_sha256':digest(raw_path),'raw_bytes':raw_path.stat().st_size,'transcript_sha256':digest(out),'transcript_bytes':out.stat().st_size,'manifest_sha256':digest(Path(m['build_dir'])/'manifest.json'),'original_full_sha256':m['original_full_sha256'],'device_fingerprint_sha256':m['device_fingerprint_sha256'],'card_geometry':geom},0o600)
    except BaseException as exc:
        failure=exc;atomic_json(rd/'failure-private.json',{'status':'failed','error':str(exc)},0o600)
    finally:
        _restore_attempt(port,bd,rd)
    if failure:raise failure
    print(json.dumps({'status':parsed.terminal['result'],'classification':replayed.verdict,'restored':'verified'}))

def aggregate(rd,bd,output):
    m=require_run(rd,bd);v=validate(rd,bd,historical=True);log=rd/'capture-private.jsonl'
    meta=json.loads((rd/'capture-metadata-private.json').read_text());proof=json.loads((rd/'restore-proof-private.json').read_text())
    raw=rd/'capture-raw-private.bin'
    if meta.get('status')!='captured' or meta.get('epoch')!=m['epoch'] or meta.get('raw_sha256')!=digest(raw) or meta.get('raw_bytes')!=raw.stat().st_size or meta.get('transcript_sha256')!=digest(log) or meta.get('transcript_bytes')!=log.stat().st_size or meta.get('manifest_sha256')!=digest(Path(m['build_dir'])/'manifest.json') or meta.get('original_full_sha256')!=m['original_full_sha256'] or meta.get('device_fingerprint_sha256')!=m['device_fingerprint_sha256']:stop('capture metadata binding differs')
    if proof.get('status')!='verified' or proof.get('full_sha256')!=m['original_full_sha256'] or proof.get('device_fingerprint_sha256')!=m['device_fingerprint_sha256']:stop('full restore proof missing')
    h35_ref=m.get('h35_reference_epoch',H35_EPOCH_LEGACY)
    md=importlib.import_module('h37_sd_metadata');raw_bytes=raw.read_bytes();ref=read_h35_identity(run_meta=m)
    extracted=md.extract_h37_record_lines(raw_bytes)
    if b''.join(extracted)!=log.read_bytes():stop('raw capture to H37 transcript extraction differs')
    plan=json.loads((rd/'read-plan-private.json').read_text());ledger=json.loads((rd/'dispatch-ledger-private.json').read_text())
    if plan.get('epoch')!=m['epoch'] or plan.get('elf_sha256')!=v['elf_sha256'] or ledger.get('epoch')!=m['epoch'] or ledger.get('elf_sha256')!=v['elf_sha256']:stop('private plan/dispatch epoch binding differs')
    plan_rows=[x for stage in plan['stages'] for x in stage]
    planned=[(int(x['lba']),int(x['count'])) for x in plan_rows]
    dispatched=[(int(x['lba']),int(x['count'])) for x in ledger['dispatches']]
    parsed=md.parse_capture(raw_bytes,m['epoch'],v['elf_sha256'],h35_ref,ref,planned,dispatched)
    result,geometry,_,_=replay_classification(parsed,rd,m['epoch'],v['elf_sha256'])
    safe=md.sanitized_summary(result,parsed)
    for part in safe.get('partitions',[]):part.pop('type_code',None)
    safe.update({'elf_sha256':v['elf_sha256'],'app_sha256':v['images']['app']['sha256'],'source_sha256':v['source']['sha256'],
        'restored':'verified','filesystem_contents':'not_inspected','media_backup':'waived_by_owner_2026-10-05',
        'media_writes':0,'geometry':{k:geometry[k] for k in ('sectors','sector_bytes','capacity_bytes','bus_width','real_freq_khz','ddr')}})
    public_fields={'epoch','verdict','scheme','signatures','partitions','read_count','read_bytes','terminal','elf_sha256','app_sha256','source_sha256','restored','filesystem_contents','media_backup','media_writes','geometry'}
    if set(safe)!=public_fields:stop('sanitized public evidence field allowlist mismatch')
    if output.exists():stop('public evidence output already exists')
    atomic_json(output,safe);print(json.dumps(safe,indent=2))

def restore(port,bd):
    backup(bd);h32.restore(port,bd)

def _restore_attempt(port,bd,rd):
    """Run the mandatory full BOX restore and bind its private proof."""
    h32.restore(port,bd)
    proof=json.loads((bd/'restore-proof-private.json').read_text())
    atomic_json(rd/'restore-proof-private.json',proof,0o600)

def host_checks():
    """Meaningful no-hardware dependency and protocol-contract preflight."""
    with tempfile.TemporaryDirectory(prefix='h37-host-check-') as td:
        serial=capture_preflight(Path(td))
        info=json.loads((Path(td)/'host-preflight-private.json').read_text())['serial']
    cases=[]
    callback_names=(('require_run',globals()),('backup',globals()),('validate',globals()),
                    ('verify_device',vars(h32)),('esptool',vars(h32)),('match_read',vars(h32)),
                    ('restore',vars(h32)),('run',vars(h32)))
    def snapshot_tree(root):
        return {str(p.relative_to(root)):(p.stat().st_mode&0o777,'dir' if p.is_dir() else p.read_bytes()) for p in sorted(root.rglob('*'))}
    for marker in RAW_MARKERS:
        calls=[];imports=[];originals=[]
        def sentinel(*args,**kwargs):calls.append(1);raise AssertionError('device callback invoked before used-epoch refusal')
        try:
            for name,scope in callback_names:originals.append((scope,name,scope[name]));scope[name]=sentinel
            with tempfile.TemporaryDirectory(prefix='h37-used-epoch-') as td:
                rd=Path(td)/'run';rd.mkdir(mode=0o700);(rd/marker).write_bytes(b'used');before=snapshot_tree(rd)
                def should_not_import(name):imports.append(name);raise AssertionError('serial import attempted for used epoch')
                try:flash_capture('sentinel-port',None,rd,should_not_import)
                except SystemExit as exc:
                    if str(exc)!='STOP: H37 capture epoch already used':stop('used epoch returned unrelated refusal: '+marker)
                else:stop('used capture epoch accepted: '+marker)
                if before!=snapshot_tree(rd):stop('used epoch directory changed: '+marker)
            if calls or imports:stop('used epoch reached import/device callback: '+marker)
            cases.append({'case':'used_'+marker,'status':'refused_unchanged','callbacks':len(calls),'imports':len(imports)})
        finally:
            for scope,name,value in reversed(originals):scope[name]=value
    # Missing serial fails closed before backup validation or any device callback.
    calls=[];originals=[];imports=[]
    def sentinel(*args,**kwargs):calls.append(1);raise AssertionError('device callback invoked during serial preflight')
    def missing_import(name):imports.append(name);raise ModuleNotFoundError("No module named 'serial'")
    try:
        for name,scope in callback_names:originals.append((scope,name,scope[name]));scope[name]=sentinel
        with tempfile.TemporaryDirectory(prefix='h37-missing-serial-') as td:
            rd=Path(td)/'run';rd.mkdir(mode=0o700)
            try:flash_capture('sentinel-port',None,rd,missing_import)
            except SystemExit as exc:
                if str(exc)!='STOP: serial import failed':stop('missing serial returned unrelated failure')
            else:stop('missing serial preflight unexpectedly passed')
            record=json.loads((rd/'host-preflight-private.json').read_text())
            if record.get('serial',{}).get('reason')!='serial_import_failed':stop('missing serial failure metadata differs')
        if calls or imports!=['serial']:stop('missing serial reached unexpected callbacks/imports')
        cases.append({'case':'missing_serial','status':'failed_closed','callbacks':len(calls),'imports':len(imports)})
    finally:
        for scope,name,value in reversed(originals):scope[name]=value
    # A distribution-valid read-only shim omits write and must fail before callbacks.
    real_serial=importlib.import_module('serial');calls=[];originals=[];imports=[]
    class ReadOnlySerial:
        def __init__(self,port=None,baudrate=0,timeout=None,write_timeout=None):self.port=port;self.is_open=False;self.dtr=False;self.rts=False;self.write_timeout=write_timeout
        def reset_input_buffer(self):pass
        def read(self,n):return b''
        def close(self):pass
        def __enter__(self):return self
        def __exit__(self,*args):self.close()
    bad_module=SimpleNamespace(__file__=real_serial.__file__,Serial=ReadOnlySerial)
    def missing_write_import(name):imports.append(name);return bad_module
    try:
        for name,scope in callback_names:originals.append((scope,name,scope[name]));scope[name]=sentinel
        with tempfile.TemporaryDirectory(prefix='h37-missing-write-') as td:
            rd=Path(td)/'run';rd.mkdir(mode=0o700)
            try:flash_capture('sentinel-port',None,rd,missing_write_import)
            except SystemExit as exc:
                if str(exc)!='STOP: pyserial write API invalid':stop('missing write API returned unrelated failure')
            else:stop('missing write API unexpectedly passed')
            record=json.loads((rd/'host-preflight-private.json').read_text())
            if record.get('serial',{}).get('reason')!='pyserial_write_api_invalid':stop('missing write API metadata differs')
        if calls or imports!=['serial']:stop('missing write API reached device callback/import')
        cases.append({'case':'missing_write','status':'failed_closed','callbacks':len(calls),'imports':len(imports)})
    finally:
        for scope,name,value in reversed(originals):scope[name]=value
    # The operation's finally restoration must propagate a restore failure.
    original_restore=h32.restore;restore_calls=[]
    def failed_restore(*args,**kwargs):restore_calls.append(1);raise RuntimeError('mock restore failure')
    h32.restore=failed_restore
    try:
        with tempfile.TemporaryDirectory(prefix='h37-finally-restore-') as td:
            try:_restore_attempt('sentinel-port',Path(td)/'backup',Path(td)/'run')
            except RuntimeError as exc:
                if str(exc)!='mock restore failure':stop('restore failure changed unexpectedly')
            else:stop('restore failure was suppressed')
    finally:h32.restore=original_restore
    if restore_calls!=[1]:stop('finally restore callback count differs')
    cases.append({'case':'restore_failure_propagates','status':'pass','restore_calls':len(restore_calls)})
    # Inject a failure at the first app flash inside the real flash_capture try/finally.
    with tempfile.TemporaryDirectory(prefix='h37-flash-finally-') as td:
        root=Path(td);rd=root/'run';rd.mkdir(mode=0o700);bd=root/'backup';bd.mkdir(mode=0o700)
        app=rd/'fake-app.bin';app.write_bytes(b'fixture-app')
        full=rd/'fixture-full.bin'
        with full.open('wb') as f:f.truncate(h32.FLASH)
        bind={'full':{'bytes':h32.FLASH,'sha256':digest(full)},'device':{'chip':'esp32-s3','flash_bytes':h32.FLASH,'fingerprint_sha256':'a'*64}}
        manifest={'epoch':'b'*32,'build_dir':str(rd/'build')}
        images={'images':{'app':{'path':str(app),'sha256':digest(app)}},'elf_sha256':'c'*64,'source':{'sha256':'e'*64}}
        originals=[];restore_calls=[];esptool_calls=[]
        def assign(scope,name,value):originals.append((scope,name,scope[name]));scope[name]=value
        def fake_backup(path):return bind
        def fake_run(path,backup_dir):return manifest
        def fake_validate(path,backup_dir):return images
        def fake_identity(**_kwargs):return 'd'*64
        def fake_verify(*args,**kwargs):return {'chip':'esp32-s3','chip_description':'ESP32-S3 fixture','flash_manufacturer':'fixture','flash_device':'fixture','flash_bytes':h32.FLASH,'fingerprint_sha256':bind['device']['fingerprint_sha256'],'mac':'00:00:00:00:00:00'}
        def fake_esptool(port,args,*rest,**kwargs):
            esptool_calls.append(args)
            if 'read_flash' in args:
                proof_path=rd/'current-device-proof-private.json'
                if not proof_path.is_file() or proof_path.stat().st_mode&0o777!=0o600:stop('current device proof missing or not private before full read')
                proof_record=json.loads(proof_path.read_text())
                if proof_record.get('epoch')!=manifest['epoch'] or proof_record.get('source_sha256')!='e'*64 or proof_record.get('elf_sha256')!=images['elf_sha256'] or proof_record.get('backup_device_fingerprint_sha256')!=bind['device']['fingerprint_sha256'] or proof_record.get('matches_backup') is not True or proof_record.get('live_device',{}).get('mac') is not None:stop('current device proof binding or field allowlist differs')
                dest=Path(args[-1])
                with dest.open('wb') as f:f.truncate(h32.FLASH)
                return None
            raise RuntimeError('mock app write failure')
        def fake_restore(port,backup_dir):
            restore_calls.append(1)
            atomic_json(Path(backup_dir)/'restore-proof-private.json',{'status':'verified','full_sha256':bind['full']['sha256'],'device_fingerprint_sha256':bind['device']['fingerprint_sha256']},0o600)
        try:
            assign(globals(),'backup',fake_backup);assign(globals(),'require_run',fake_run);assign(globals(),'validate',fake_validate);assign(globals(),'read_h35_identity',fake_identity)
            assign(vars(h32),'verify_device',fake_verify);assign(vars(h32),'esptool',fake_esptool);assign(vars(h32),'restore',fake_restore)
            try:flash_capture('sentinel-port',bd,rd)
            except RuntimeError as exc:
                if str(exc)!='mock app write failure':stop('injected flash failure changed unexpectedly')
            else:stop('injected app write failure unexpectedly passed')
        finally:
            for scope,name,value in reversed(originals):scope[name]=value
        if len(restore_calls)!=1 or len(esptool_calls)!=2 or not (rd/'restore-proof-private.json').is_file():
            stop('flash_capture finally did not invoke/bind BOX restore after injected failure')
    cases.append({'case':'flash_failure_invokes_finally_restore','status':'pass','restore_calls':len(restore_calls)})
    # Host writes rely on pyserial's bounded write_timeout; never invoke its
    # POSIX flush/termios.tcdrain path, which has no timeout guarantee.
    class CommandPort:
        def __init__(self,result=None,error=None):self.result=result;self.error=error;self.writes=[];self.flush_calls=0
        def write(self,data):
            self.writes.append(data)
            if self.error:raise self.error
            return len(data) if self.result is None else self.result
        def flush(self):self.flush_calls+=1;raise AssertionError('blocking serial flush must not be called')
    good=CommandPort();_send(good,'H37C,1,FINISH,'+'a'*32+','+'b'*64+',1\n')
    if good.flush_calls or len(good.writes)!=1 or not good.writes[0].endswith(b'\n'):stop('bounded serial command write contract failed')
    short=CommandPort(result=2)
    try:_send(short,'H37C,1,FINISH\n')
    except SystemExit as exc:
        if str(exc)!='STOP: short command write':stop('short serial write returned unrelated error')
    else:stop('short serial write unexpectedly passed')
    if short.flush_calls:stop('short serial write invoked flush')
    oversized=CommandPort()
    try:_send(oversized,'X'*256+'\n')
    except SystemExit as exc:
        if str(exc)!='STOP: host command exceeds frozen bound':stop('oversized command returned unrelated error')
    else:stop('oversized serial command unexpectedly passed')
    if oversized.writes or oversized.flush_calls:stop('oversized serial command reached transport')
    broken=CommandPort(error=RuntimeError('mock serial write failure'))
    try:_send(broken,'H37C,1,FINISH\n')
    except RuntimeError as exc:
        if str(exc)!='mock serial write failure':stop('serial write exception changed unexpectedly')
    else:stop('serial write exception was suppressed')
    if broken.flush_calls:stop('failed serial write invoked flush')
    cases.append({'case':'bounded_command_write_no_flush','status':'pass','short_write':'rejected','oversize':'rejected','write_exception':'propagated'})
    # Metadata utility exposes an executable synthetic suite; no hardware is involved.
    check=importlib.import_module('h37_sd_metadata_checks');check_count=check.main()
    if check_count!=0:stop('metadata synthetic checks failed')
    print(json.dumps({'status':'pass','synthetic_only':True,'serial_distribution':info['distribution'],'serial_version':info['version'],'controller_preflight_checks':cases},indent=2))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['prepare','build','validate','flash-capture','restore','parse','host-checks']);p.add_argument('--port');p.add_argument('--backup-dir',type=Path,default=H35_BACKUP);p.add_argument('--run-dir',type=Path);p.add_argument('--output',type=Path);p.add_argument('--h35-capture-dir',type=Path,help='private H35 capture directory (else env/pointer/default)');a=p.parse_args()
    if a.h35_capture_dir is not None:set_h35_capture_dir_cli(a.h35_capture_dir)
    if a.command=='host-checks':host_checks();return
    if a.command=='restore':restore(h32.one_port(a.port),a.backup_dir);return
    if not a.run_dir:p.error('--run-dir required')
    if a.command=='prepare':prepare(a.run_dir,a.backup_dir,a.h35_capture_dir)
    elif a.command=='build':build(a.run_dir,a.backup_dir)
    elif a.command=='validate':print(json.dumps(validate(a.run_dir,a.backup_dir),indent=2))
    elif a.command=='flash-capture':flash_capture(h32.one_port(a.port),a.backup_dir,a.run_dir)
    elif a.command=='parse':
        if not a.output:p.error('--output required')
        aggregate(a.run_dir,a.backup_dir,a.output)

if __name__=='__main__':main()
