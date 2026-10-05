#!/usr/bin/env python3
"""Private, fail-closed H35 controller. Discovery never authorizes media writes."""
from __future__ import annotations
import argparse,json,os,re,secrets,subprocess,time
from pathlib import Path
import h32_storage_qual as h32
ROOT=h32.ROOT; FW=h32.FW
stop=h32.stop; digest=h32.digest; atomic_json=h32.atomic_json


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
    factory=[p for p in ps if p['name']=='factory' and p['type']==0 and p['subtype']==0 and p['offset']==h32.APP_OFF]
    if len(factory)!=1: stop('original factory slot required')
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
    images={n:{'path':str(p),'bytes':p.stat().st_size,'sha256':digest(p),'offset':off} for n,p,off in [('app',d/'family_link_demo.bin',h32.APP_OFF),('partition_table',d/'partition_table/partition-table.bin',h32.PT_OFF),('bootloader',d/'bootloader/bootloader.bin',0)]}
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
    factory=next(p for p in ps if p['name']=='factory' and p['type']==0 and p['subtype']==0)
    if factory!=v['factory'] or factory['offset']!=h32.APP_OFF or v['images']['app']['offset']!=factory['offset'] or v['images']['app']['bytes']>factory['size']*85//100: stop('factory bounds/headroom invalid')
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


def flash_capture(port,bd,rd,seconds):
    if not 35<=seconds<=120: stop('capture bound must be 35..120 seconds')
    m=require_run(rd,bd); b=backup(bd); v=validate(rd,bd); out=rd/'capture-private.txt'
    if (rd/'flash-attempt-private.json').exists() or out.exists(): stop('epoch already used')
    # Re-read complete original image now: this establishes freshness, including all NVS.
    h32.verify_device(port,b,hold=True)
    fresh=rd/'preflash-full-private.bin'
    h32.esptool(port,['--after','no_reset','read_flash','0',hex(h32.FLASH),str(fresh)],1200)
    if fresh.stat().st_size!=h32.FLASH or digest(fresh)!=b['full']['sha256']: stop('connected flash differs from original backup')
    app=Path(v['images']['app']['path'])
    atomic_json(rd/'flash-attempt-private.json',{'epoch':m['epoch'],'app_sha256':digest(app),'offset':h32.APP_OFF,'bytes':app.stat().st_size,'removable_writes':'none','status':'attempted'},0o600)
    failure=None
    try:
        h32.esptool(port,['--after','no_reset','write_flash','--flash_size','16MB',hex(h32.APP_OFF),str(app)])
        h32.match_read(port,h32.APP_OFF,app,rd/'app-readback-private.bin')
        # Verify every byte outside sector-rounded application erase/write range.
        check=rd/'postflash-full-private.bin'
        h32.esptool(port,['--after','no_reset','read_flash','0',hex(h32.FLASH),str(check)],1200)
        original=fresh.read_bytes(); changed=check.read_bytes(); end=h32.APP_OFF+((app.stat().st_size+4095)//4096)*4096
        if len(changed)!=h32.FLASH or changed[:h32.APP_OFF]!=original[:h32.APP_OFF] or changed[end:]!=original[end:]: stop('flash changed bytes outside application erase sectors')
        h32.verify_device(port,b,hold=True)
        import serial
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
    print(json.dumps({'status':'pass','synthetic_only':True,'rejected':list(variants)}))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['inventory','prepare-run','build','validate-build','flash-capture','parse','restore','host-checks']);p.add_argument('--port');p.add_argument('--backup-dir',type=Path);p.add_argument('--run-dir',type=Path);p.add_argument('--output',type=Path);p.add_argument('--seconds',type=int,default=45);a=p.parse_args()
    if a.command=='host-checks':host_checks();return
    if a.command=='inventory':h32.inventory(h32.one_port(a.port));return
    if not a.backup_dir:p.error('--backup-dir required')
    if a.command=='restore':backup(a.backup_dir);h32.restore(h32.one_port(a.port),a.backup_dir);return
    if not a.run_dir:p.error('--run-dir required')
    if a.command=='prepare-run':prepare(a.run_dir,a.backup_dir)
    elif a.command=='build':build(a.run_dir,a.backup_dir)
    elif a.command=='validate-build':print(json.dumps(validate(a.run_dir,a.backup_dir),indent=2))
    elif a.command=='flash-capture':flash_capture(h32.one_port(a.port),a.backup_dir,a.run_dir,a.seconds)
    elif a.command=='parse':
        if not a.output:p.error('--output required')
        aggregate(a.run_dir,a.backup_dir,a.output)
if __name__=='__main__':main()
