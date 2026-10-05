#!/usr/bin/env python3
"""Fail-closed controller for h32. Private device data stays outside the repo."""
from __future__ import annotations
import argparse,csv,hashlib,json,os,re,secrets,shlex,signal,struct,subprocess,tempfile,time
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent; FW=ROOT/"firmware"
EXP=FW/"build/h32_onchip_storage"; SENT=FW/"build/h32_onchip_storage_sentinel"
FIXTURE=FW/"partitions/h32_onchip_storage.csv"
IDF=Path(os.environ.get("IDF_PATH",Path.home()/"esp/esp-idf"))
FLASH=0x1000000; PT_OFF=0x8000; PT_SIZE=0x1000; APP_OFF=0x10000; APP_SIZE=0x220000
OUT_OFF=0x230000; OUT_SIZE=0xDD0000
PROFILES=("baseline","no_yield","bounded_yield")
PROFILE_FILES={p:Path(f"firmware/sdkconfig.h32.{p}.defaults") for p in PROFILES}
EPOCH_RE=re.compile(r"[0-9a-f]{32}\Z")
# Fail-fast capture watchdogs (orchestrator should not block on a dead port or panic loop).
CAPTURE_FIRST_BYTE_S=45
CAPTURE_BYTE_STALL_S=90
CAPTURE_MOUNT_MILESTONE_S=240
CAPTURE_SENTINEL_READY_S=30
CAPTURE_GURU_MAX=3
CAPTURE_SENTINEL_DEFAULT_S=30
CAPTURE_EXPERIMENT_DEFAULT_S=5400
EXPECTED=[("nvs",1,2,0x9000,0x6000),("phy_init",1,1,0xf000,0x1000),
          ("factory",0,0,APP_OFF,APP_SIZE),("outbox",1,0x81,OUT_OFF,OUT_SIZE)]
SOURCE_INPUTS=(Path("firmware/main/CMakeLists.txt"),Path("firmware/CMakeLists.txt"),Path("firmware/sdkconfig.defaults"),Path("firmware/demos/h32_onchip_storage.c"),
  Path("firmware/sdkconfig.h32.defaults"),Path("firmware/partitions/h32_onchip_storage.csv"),
  Path("scripts/h32_storage_qual.py"),*PROFILE_FILES.values())

def stop(s): raise SystemExit("STOP: "+s)
def run(args,timeout=1800,capture=False):
    cmd=f"set -euo pipefail; source {shlex.quote(str(IDF/'export.sh'))} >/dev/null; {shlex.join(args)}"
    return subprocess.run(["bash","-c",cmd],cwd=ROOT,timeout=timeout,check=True,text=True,capture_output=capture)
def esptool(port,args,timeout=900,capture=False):
    return run(["python","-m","esptool","--chip","esp32s3","-p",port,"-b","460800",*args],timeout,capture)
def digest(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda:f.read(1<<20),b""): h.update(b)
    return h.hexdigest()
def dh(data): return hashlib.sha256(data).hexdigest()
def atomic_json(path,data,mode=None):
    path.parent.mkdir(parents=True,exist_ok=True); fd,tmp=tempfile.mkstemp(dir=path.parent,prefix="."+path.name)
    try:
        with os.fdopen(fd,"w") as f:
            json.dump(data,f,indent=2,sort_keys=True); f.write("\n");f.flush();os.fsync(f.fileno())
        os.replace(tmp,path)
        if mode: path.chmod(mode)
        parent_fd=os.open(path.parent,os.O_RDONLY)
        try: os.fsync(parent_fd)
        finally: os.close(parent_fd)
    finally:
        try: os.unlink(tmp)
        except FileNotFoundError: pass
def erased_sha256():
    h=hashlib.sha256(); block=b"\xff"*(1<<20); left=OUT_SIZE
    while left:
        n=min(left,len(block)); h.update(block[:n]); left-=n
    return h.hexdigest()
ERASED_SHA256=erased_sha256()
def require_run(rd,bd=None):
    if rd is None or rd.resolve().is_relative_to(ROOT.resolve()): stop("--run-dir outside repository required")
    try: m=json.loads((rd/"operator-run-private.json").read_text())
    except Exception as e: stop(f"private run metadata missing/invalid: {e}; prepare-run first")
    if m.get("schema")!=2 or m.get("profile") not in PROFILES or not EPOCH_RE.fullmatch(m.get("epoch", "")): stop("invalid requested run epoch")
    if bd is not None:
        b=require_backup(bd)
        if m.get("original_full_sha256")!=b["full"]["sha256"] or m.get("device_fingerprint_sha256")!=b["device"]["fingerprint_sha256"]: stop("run is bound to another original backup/device")
    select_run_builds(m)
    return m
def prepare_run(rd,bd,profile):
    if rd is None or rd.resolve().is_relative_to(ROOT.resolve()): stop("--run-dir outside repository required")
    b=require_backup(bd)
    if rd.exists(): stop("run directory already exists; refusing to regenerate epoch or reuse evidence")
    rd.mkdir(parents=True,mode=0o700); rd.chmod(0o700)
    old_log=ROOT/"logs/h32-experiment-probe.txt";old_side=old_log.with_name(old_log.name+".metadata.json")
    failed=None
    if old_log.is_file() and old_side.is_file():
        old_text=old_log.read_text(errors="replace")
        old_meta=json.loads(old_side.read_text())
        failure=[line[line.find("H32,FAIL,"):] for line in old_text.splitlines() if "H32,FAIL," in line]
        old_manifest=EXP/"h32-build-manifest.json"
        failed={"status":"nonqualifying","probe_log_sha256":digest(old_log),"probe_metadata_sha256":digest(old_side),
                "mount_failure":failure[-1] if failure else None,"old_requested_epoch":old_meta.get("epoch"),
                "old_source_sha256":old_meta.get("source",{}).get("sha256"),
                "old_app_sha256":old_meta.get("images",{}).get("app",{}).get("sha256"),
                "old_partition_table_sha256":old_meta.get("images",{}).get("partition_table",{}).get("sha256"),
                "old_manifest_sha256":digest(old_manifest) if old_manifest.is_file() else None}
    m={"schema":2,"profile":profile,"epoch":secrets.token_hex(16),"status":"prepared","created_unix":int(time.time()),"failed_attempt":failed,
       "original_partition_sha256":b["partition_table"]["sha256"],"original_nvs_sha256":b["nvs"]["sha256"],"original_full_sha256":b["full"]["sha256"],"device_fingerprint_sha256":b["device"]["fingerprint_sha256"]}
    atomic_json(rd/"operator-run-private.json",m,0o600)
    print(json.dumps({"status":"prepared","epoch":m["epoch"],"run_dir":str(rd)},indent=2))
def build_paths(run_meta):
    suffix=run_meta["profile"]+"-"+run_meta["epoch"]
    return (FW/"build"/("h32_onchip_storage-"+suffix),FW/"build"/("h32_onchip_storage_sentinel-"+suffix))
def select_run_builds(run_meta):
    global EXP,SENT
    EXP,SENT=build_paths(run_meta)
def update_run(rd,m): atomic_json(rd/"operator-run-private.json",m,0o600)
def one_port(p):
    import glob
    ports=[p] if p else sorted(glob.glob("/dev/cu.usbmodem*")); ports=[x for x in ports if x]
    if len(ports)!=1: stop(f"expected one target, found {ports}")
    return ports[0]
def field(pattern,text,name):
    m=re.search(pattern,text,re.I|re.M)
    if not m: stop(f"could not read {name} from esptool inventory")
    return m.group(1).strip().lower()
def probe(port,hold=False):
    prefix=["--after","no_reset"] if hold else []
    a=esptool(port,[*prefix,"chip_id"],capture=True); b=esptool(port,[*prefix,"flash_id"],capture=True)
    text="\n".join((a.stdout,a.stderr,b.stdout,b.stderr)); chip=field(r"Chip is\s+([^\r\n]+)",text,"chip")
    if "esp32-s3" not in chip: stop("target is not ESP32-S3")
    if field(r"Detected flash size:\s*([^\s]+)",text,"flash size") not in ("16mb","16mib"): stop("target is not 16 MiB")
    d={"chip":"esp32-s3","chip_description":chip,"mac":field(r"MAC:\s*([0-9a-f:]{17})",text,"MAC"),
       "flash_manufacturer":field(r"Manufacturer:\s*([^\r\n]+)",text,"manufacturer"),
       "flash_device":field(r"Device:\s*([^\r\n]+)",text,"flash device"),"flash_bytes":FLASH}
    d["fingerprint_sha256"]=dh(json.dumps(d,sort_keys=True).encode()); return d
def verify_device(port,meta,hold=False):
    d=probe(port,hold)
    if d["fingerprint_sha256"]!=meta.get("device",{}).get("fingerprint_sha256"): stop("connected device does not match backup")
    return d
def inventory(port):
    d=probe(port); print(json.dumps({"status":"verified","port":port,"chip":d["chip_description"],
      "flash_bytes":FLASH,"device_fingerprint_sha256":d["fingerprint_sha256"]},indent=2))

def cstr(b): return b.split(b"\0",1)[0].decode(errors="replace")
def partitions(blob):
    out=[]
    for at in range(0,min(len(blob),PT_SIZE),32):
        e=blob[at:at+32]
        if len(e)<32 or e==b"\xff"*32: break
        magic=struct.unpack_from("<H",e)[0]
        if magic==0xebeb: break
        if magic!=0x50aa: stop(f"invalid partition magic at 0x{at:x}")
        off,size=struct.unpack_from("<II",e,4); name=cstr(e[12:28])
        if not name or not size or off+size>FLASH: stop(f"invalid partition bounds: {name}")
        out.append({"name":name,"type":e[2],"subtype":e[3],"offset":off,"size":size,"flags":struct.unpack_from("<I",e,28)[0]})
    if not out: stop("no partition entries")
    ordered=sorted(out,key=lambda x:x["offset"])
    for a,b in zip(ordered,ordered[1:]):
        if a["offset"]+a["size"]>b["offset"]: stop("overlapping partitions")
    return out
def tup(p): return p["name"],p["type"],p["subtype"],p["offset"],p["size"]
def exact_table(ps):
    if [tup(p) for p in ps]!=EXPECTED: stop("partition table differs from exact fixture")
def region(path,off,size):
    with path.open("rb") as f: f.seek(off); b=f.read(size)
    if len(b)!=size: stop(f"short region 0x{off:x}+0x{size:x}")
    return b
def app_desc(blob):
    at=blob.find(struct.pack("<I",0xabcd5432))
    if at<0 or at+176>len(blob): return None
    return {"version":cstr(blob[at+16:at+48]),"project":cstr(blob[at+48:at+80]),
            "idf":cstr(blob[at+112:at+144]),"elf_sha256":blob[at+144:at+176].hex()}

def backup(port,out):
    if out.resolve().is_relative_to(ROOT.resolve()): stop("backup must be outside repository")
    dev=probe(port); out.mkdir(parents=True,exist_ok=False); out.chmod(0o700); full=out/"original-flash.bin"
    esptool(port,["read_flash","0",hex(FLASH),str(full)],1200)
    if full.stat().st_size!=FLASH: stop("wrong full backup size")
    ps=partitions(region(full,PT_OFF,PT_SIZE)); n=[p for p in ps if tup(p)[:3]==("nvs",1,2)]
    factory=[p for p in ps if p["name"]=="factory" and p["type"]==0 and p["offset"]==APP_OFF]
    if len(n)!=1 or len(factory)!=1: stop("sentinel requires one nvs and factory app at 0x10000")
    pt=out/"original-partitions.bin"; nv=out/"original-nvs.bin"; n=n[0]
    esptool(port,["read_flash",hex(PT_OFF),hex(PT_SIZE),str(pt)])
    esptool(port,["read_flash",hex(n["offset"]),hex(n["size"]),str(nv)])
    if pt.read_bytes()!=region(full,PT_OFF,PT_SIZE) or nv.read_bytes()!=region(full,n["offset"],n["size"]): stop("independent readback differs")
    apps=[]
    for p in ps:
        if p["type"]==0:
            b=region(full,p["offset"],p["size"]); apps.append({**p,"partition_sha256":dh(b),"descriptor":app_desc(b)})
    meta={"schema":2,"initial_port":port,"device":dev,"full":{"bytes":FLASH,"sha256":digest(full)},
      "partition_table":{"offset":PT_OFF,"bytes":PT_SIZE,"sha256":digest(pt),"entries":ps},
      "nvs":{"offset":n["offset"],"bytes":n["size"],"sha256":digest(nv)},"apps":apps}
    atomic_json(out/"operator-private.json",meta,0o600)
    print(json.dumps({"status":"verified","full_sha256":meta["full"]["sha256"],"device_fingerprint_sha256":dev["fingerprint_sha256"]},indent=2))
def require_backup(path):
    if path is None or path.resolve().is_relative_to(ROOT.resolve()): stop("backup directory outside repository required")
    try: m=json.loads((path/"operator-private.json").read_text())
    except Exception as e: stop(f"invalid backup metadata: {e}")
    if m.get("schema")!=2: stop("unsupported backup schema")
    for f,info in ((path/"original-flash.bin",m["full"]),(path/"original-partitions.bin",m["partition_table"]),(path/"original-nvs.bin",m["nvs"])):
        if not f.is_file() or f.stat().st_size!=info["bytes"] or digest(f)!=info["sha256"]: stop(f"backup verification failed: {f.name}")
    full=path/"original-flash.bin"
    if region(full,PT_OFF,PT_SIZE)!=(path/"original-partitions.bin").read_bytes(): stop("partition backup mismatch")
    if region(full,m["nvs"]["offset"],m["nvs"]["bytes"])!=(path/"original-nvs.bin").read_bytes(): stop("NVS backup mismatch")
    if partitions((path/"original-partitions.bin").read_bytes())!=m["partition_table"]["entries"]: stop("partition metadata mismatch")
    return m

def git_head(): return subprocess.run(["git","rev-parse","HEAD"],cwd=ROOT,text=True,capture_output=True,check=True).stdout.strip()
def idf_version(): return run(["idf.py","--version"],capture=True).stdout.strip()
def source_identity():
    files={str(p):digest(ROOT/p) for p in SOURCE_INPUTS}
    combined=dh(json.dumps(files,sort_keys=True,separators=(",",":")).encode())
    dirty=bool(subprocess.run(["git","status","--porcelain","--",*[str(p) for p in SOURCE_INPUTS]],cwd=ROOT,text=True,capture_output=True,check=True).stdout.strip())
    return {"files":files,"sha256":combined,"git_dirty":dirty}
def build_one(d,sentinel,epoch,profile):
    before=source_identity()
    d.mkdir(parents=True,exist_ok=True);sdk=d/"sdkconfig";stamp=d/"h32-config-inputs.sha256"
    config_id=dh((digest(FW/"sdkconfig.defaults")+digest(FW/"sdkconfig.h32.defaults")+digest(ROOT/PROFILE_FILES[profile])+epoch+profile+str(sentinel)).encode())
    if sdk.exists() and (not stamp.exists() or stamp.read_text().strip()!=config_id): sdk.unlink()
    run(["idf.py","-D","FAMILY_DEMO=h32_onchip_storage","-D",f"H32_SENTINEL_ONLY={1 if sentinel else 0}",
      "-D",f"H32_RUN_EPOCH={epoch}","-D",f"H32_PROFILE={profile}",
      "-D",f"SDKCONFIG={sdk}","-D",f"SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.h32.defaults;{PROFILE_FILES[profile].name}","-B",str(d),"-C",str(FW),"build"])
    if source_identity()!=before: stop("source changed during compilation; rebuild before manifest acceptance")
    atomic_json(d/"h32-compiled-source.json",before)
    stamp.write_text(config_id+"\n")
def config_settings(path,profile):
    settings={}
    for line in path.read_text().splitlines():
        match=re.fullmatch(r"(CONFIG_[A-Z0-9_]+)=(.*)",line)
        unset=re.fullmatch(r"# (CONFIG_[A-Z0-9_]+) is not set",line)
        if match: settings[match[1]]=match[2]
        elif unset: settings[unset[1]]="n"
    expected={"CONFIG_SPI_FLASH_YIELD_DURING_ERASE":"n" if profile=="no_yield" else "y",
              "CONFIG_FREERTOS_HZ":"100","CONFIG_ESP_INT_WDT":"y","CONFIG_ESP_INT_WDT_TIMEOUT_MS":"300",
              "CONFIG_ESP_INT_WDT_CHECK_CPU1":"y","CONFIG_ESP_TASK_WDT_EN":"n",
              "CONFIG_WL_SECTOR_SIZE_4096":"y","CONFIG_FATFS_SECTOR_4096":"y",
              "CONFIG_FATFS_LFN_HEAP":"y","CONFIG_FATFS_MAX_LFN":"64","CONFIG_FATFS_PER_FILE_CACHE":"y"}
    if profile!="no_yield": expected.update(CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS="100" if profile=="bounded_yield" else "20",CONFIG_SPI_FLASH_ERASE_YIELD_TICKS="1")
    if any(settings.get(k)!=v for k,v in expected.items()): stop("effective configuration differs from selected H32 profile/watchdog/FAT contract")
    selected={k:v for k,v in settings.items() if k.startswith(("CONFIG_SPI_FLASH_","CONFIG_FATFS_","CONFIG_WL_","CONFIG_ESP_INT_WDT","CONFIG_ESP_TASK_WDT")) or k=="CONFIG_FREERTOS_HZ"}
    return {"sha256":digest(path),"settings":selected}
def manifest(d,mode,epoch,create=False,profile=None,historical=False):
    mp=d/"h32-build-manifest.json"
    if profile not in PROFILES: stop("explicit valid build profile required")
    effective=config_settings(d/"sdkconfig",profile)
    if create:
        fs={"bootloader":(0,d/"bootloader/bootloader.bin"),"partition_table":(PT_OFF,d/"partition_table/partition-table.bin"),"app":(APP_OFF,d/"family_link_demo.bin")}
        if any(not p.is_file() for _,p in fs.values()): stop(f"{mode} build missing image")
        ps=partitions(fs["partition_table"][1].read_bytes()); exact_table(ps)
        if fs["app"][1].stat().st_size>int(APP_SIZE*.85): stop("application violates 15% headroom")
        if json.loads((d/"h32-compiled-source.json").read_text())!=source_identity(): stop("source changed after compilation; rebuild")
        elf=d/"family_link_demo.elf"
        if not elf.is_file() or app_desc(fs["app"][1].read_bytes())["elf_sha256"]!=digest(elf): stop("application ELF descriptor does not match actual ELF")
        v={"schema":3,"profile":profile,"effective_config":effective,"elf_sha256":digest(elf),"app_descriptor":app_desc(fs["app"][1].read_bytes()),"mode":mode,"epoch":epoch,"git_commit":git_head(),"source":source_identity(),"idf_version":idf_version(),"fixture_sha256":digest(FIXTURE),"partitions":ps,
          "images":{n:{"offset":o,"bytes":p.stat().st_size,"sha256":digest(p),"path":str(p.relative_to(ROOT))} for n,(o,p) in fs.items()}}
        for relative,sha in v["source"]["files"].items():
            original=ROOT/relative; frozen=d/"source-snapshot"/relative
            frozen.parent.mkdir(parents=True,exist_ok=True);frozen.write_bytes(original.read_bytes())
            if digest(frozen)!=sha: stop("source changed during build manifest snapshot")
        atomic_json(mp,v); return v
    try: v=json.loads(mp.read_text())
    except Exception as e: stop(f"invalid {mode} build manifest: {e}; run build")
    if v.get("schema")!=3 or v.get("profile")!=profile or v.get("effective_config")!=effective or v.get("mode")!=mode or v.get("epoch")!=epoch or digest(FIXTURE)!=v.get("fixture_sha256") or (not historical and (v.get("source")!=source_identity() or v.get("idf_version")!=idf_version())): stop("build epoch/mode/IDF/fixture/source identity mismatch; rebuild")
    if digest(d/"family_link_demo.elf")!=v.get("elf_sha256"): stop("archived ELF hash mismatch")
    for relative,sha in v["source"]["files"].items():
        frozen=d/"source-snapshot"/relative
        if not frozen.is_file() or digest(frozen)!=sha: stop("archived build source snapshot mismatch")
    ps=partitions((d/"partition_table/partition-table.bin").read_bytes());exact_table(ps)
    if ps!=v.get("partitions"): stop("build partition manifest mismatch")
    for x in v["images"].values():
        p=ROOT/x["path"]
        if not p.is_file() or p.stat().st_size!=x["bytes"] or digest(p)!=x["sha256"] or x["offset"]+x["bytes"]>FLASH: stop("build image/hash/bounds mismatch")
    if app_desc((ROOT/v["images"]["app"]["path"]).read_bytes())!=v.get("app_descriptor"): stop("build app descriptor mismatch")
    if v["images"]["app"]["bytes"]>int(APP_SIZE*.85): stop("application violates 15% headroom")
    return v
def build(epoch,profile):
    exp_dir,sent_dir=build_paths({"epoch":epoch,"profile":profile})
    if exp_dir.resolve()==sent_dir.resolve(): stop("sentinel and experiment build directories overlap")
    build_one(exp_dir,False,epoch,profile); manifest(exp_dir,"experiment",epoch,True,profile)
    build_one(sent_dir,True,epoch,profile); manifest(sent_dir,"sentinel",epoch,True,profile)
def match_read(port,off,src,dst):
    esptool(port,["--after","no_reset","read_flash",hex(off),hex(src.stat().st_size),str(dst)])
    if digest(dst)!=digest(src) or dst.stat().st_size!=src.stat().st_size: stop(f"flash readback mismatch at 0x{off:x}")
def bounds(off,size,lo,hi,label):
    if size<=0 or off<lo or off+size>hi: stop(f"{label} out of bounds")
def verify_erased_readback(path):
    if path.stat().st_size!=OUT_SIZE: stop("outbox readback has wrong byte count")
    h=hashlib.sha256(); count=0
    with path.open("rb") as f:
        for block in iter(lambda:f.read(1<<20),b""):
            count+=len(block)
            if any(x!=0xff for x in block): stop(f"outbox readback contains non-erased byte near offset {count-len(block)}")
            h.update(block)
    if count!=OUT_SIZE or h.hexdigest()!=ERASED_SHA256: stop("outbox erased-region hash mismatch")
    return {"status":"verified","offset":OUT_OFF,"bytes":count,"sha256":h.hexdigest(),"expected_sha256":ERASED_SHA256}
def flash(port,bd,sentinel,rd):
    meta=require_backup(bd); run_meta=require_run(rd,bd); epoch=run_meta["epoch"];exp_dir,sent_dir=build_paths(run_meta)
    verify_device(port,meta,hold=True); d=sent_dir if sentinel else exp_dir; mode="sentinel" if sentinel else "experiment"; m=manifest(d,mode,epoch,profile=run_meta["profile"])
    rb=bd/"readback"; rb.mkdir(mode=0o700,exist_ok=True); app=ROOT/m["images"]["app"]["path"]
    if sentinel:
        f=next((p for p in meta["partition_table"]["entries"] if p["name"]=="factory" and p["offset"]==APP_OFF),None)
        if not f: stop("original factory slot missing")
        bounds(APP_OFF,app.stat().st_size,f["offset"],f["offset"]+f["size"],"sentinel app")
        esptool(port,["--after","no_reset","write_flash","--flash_size","16MB",hex(APP_OFF),str(app)]); match_read(port,APP_OFF,app,rb/"sentinel-app.bin"); return
    proof_path=bd/"sentinel-proof-private.json"
    try: proof=json.loads(proof_path.read_text())
    except Exception as e: stop(f"validated sentinel proof missing: {e}")
    log_path=Path(proof.get("source_log_path",""))
    proof_ok=(proof.get("epoch")==epoch and proof.get("status")=="verified" and proof.get("mode")=="sentinel_only" and proof.get("storage_access")=="no" and
      proof.get("original_full_sha256")==meta["full"]["sha256"] and proof.get("original_nvs_sha256")==meta["nvs"]["sha256"] and
      proof.get("post_sentinel_nvs_sha256")==meta.get("post_sentinel_nvs",{}).get("sha256") and
      proof.get("device_fingerprint_sha256")==meta["device"]["fingerprint_sha256"] and
      proof.get("sentinel_app_sha256")==manifest(sent_dir,"sentinel",epoch,profile=run_meta["profile"])["images"]["app"]["sha256"] and
      log_path.is_file() and digest(log_path)==proof.get("source_log_sha256"))
    if not proof_ok: stop("sentinel proof is not semantically valid and bound to backup/device/build/NVS/log")
    if run_meta.get("erased_region") is not None: stop("this run has already erased outbox; prepare a new run for retry")
    pre=rd/"pre-experiment-nvs.bin"; esptool(port,["--after","no_reset","read_flash",hex(meta["nvs"]["offset"]),hex(meta["nvs"]["bytes"]),str(pre)])
    if pre.stat().st_size!=meta["nvs"]["bytes"] or digest(pre)!=meta["post_sentinel_nvs"]["sha256"]: stop("NVS changed after sentinel backup")
    boot=ROOT/m["images"]["bootloader"]["path"]; pt=ROOT/m["images"]["partition_table"]["path"]
    bounds(0,boot.stat().st_size,0,PT_OFF,"bootloader"); bounds(PT_OFF,pt.stat().st_size,PT_OFF,PT_OFF+PT_SIZE,"partition table"); bounds(APP_OFF,app.stat().st_size,APP_OFF,APP_OFF+APP_SIZE,"app")
    if OUT_OFF+OUT_SIZE!=FLASH: stop("outbox erase bounds invalid")
    erase_start=time.monotonic()
    esptool(port,["--after","no_reset","erase_region",hex(OUT_OFF),hex(OUT_SIZE)],1200)
    erase_elapsed=time.monotonic()-erase_start
    blank_tmp=rd/"outbox-erased-readback.bin"
    if blank_tmp.exists(): stop("outbox readback path already exists")
    try:
        read_start=time.monotonic()
        esptool(port,["--after","no_reset","read_flash",hex(OUT_OFF),hex(OUT_SIZE),str(blank_tmp)],1200)
        erased=verify_erased_readback(blank_tmp); erased["erase_elapsed_s"]=erase_elapsed;erased["readback_elapsed_s"]=time.monotonic()-read_start
        erased["command"]=["erase_region",hex(OUT_OFF),hex(OUT_SIZE)]
        run_meta["erased_region"]=erased; update_run(rd,run_meta)
    finally:
        if run_meta.get("erased_region",{}).get("status")=="verified": blank_tmp.unlink(missing_ok=True)
    esptool(port,["--after","no_reset","write_flash","--flash_size","16MB","0x0",str(boot),hex(PT_OFF),str(pt),hex(APP_OFF),str(app)])
    for name,off,src in (("boot",0,boot),("partitions",PT_OFF,pt),("app",APP_OFF,app)): match_read(port,off,src,rb/(name+".bin"))
    post=rd/"post-experiment-nvs.bin"; esptool(port,["--after","no_reset","read_flash",hex(meta["nvs"]["offset"]),hex(meta["nvs"]["bytes"]),str(post)])
    if post.stat().st_size!=meta["nvs"]["bytes"] or digest(post)!=meta["post_sentinel_nvs"]["sha256"]: stop("NVS changed during experiment flash")
    run_meta["experiment_flash"]={"status":"verified","post_flash_nvs_sha256":digest(post),"partition_table_sha256":m["images"]["partition_table"]["sha256"],"app_sha256":m["images"]["app"]["sha256"]};update_run(rd,run_meta)
    # Keep the verified application in ROM until capture opens the serial port.
def sentinel_backup(port,bd,rd):
    epoch=require_run(rd,bd)["epoch"]
    m=require_backup(bd); verify_device(port,m,hold=True); p=bd/"post-sentinel-nvs.bin"; esptool(port,["--after","no_reset","read_flash",hex(m["nvs"]["offset"]),hex(m["nvs"]["bytes"]),str(p)])
    if p.stat().st_size!=m["nvs"]["bytes"] or digest(p)==m["nvs"]["sha256"]: stop("sentinel NVS change not proven")
    proof_path=bd/"sentinel-proof-private.json"
    try: proof=json.loads(proof_path.read_text())
    except Exception as e: stop(f"validated sentinel log proof missing: {e}")
    log_path=Path(proof.get("source_log_path",""))
    if proof.get("epoch")!=epoch or proof.get("status")!="verified" or proof.get("mode")!="sentinel_only" or proof.get("storage_access")!="no" or proof.get("device_fingerprint_sha256")!=m["device"]["fingerprint_sha256"] or proof.get("original_full_sha256")!=m["full"]["sha256"] or proof.get("original_nvs_sha256")!=m["nvs"]["sha256"] or not log_path.is_file() or digest(log_path)!=proof.get("source_log_sha256"): stop("sentinel proof semantic/backup/log binding failed")
    nvs_hash=digest(p);m["post_sentinel_nvs"]={"bytes":p.stat().st_size,"sha256":nvs_hash}
    proof["post_sentinel_nvs_sha256"]=nvs_hash
    atomic_json(bd/"operator-private.json",m,0o600);atomic_json(proof_path,proof,0o600)
    print(json.dumps({"status":"verified","sha256":nvs_hash},indent=2))

def complete_capture_lines(pending,chunk):
    pieces=(pending+chunk).split(b"\n")
    return pieces[-1],pieces[:-1]
def capture(port,bd,rd,out,seconds,mode):
    if not 1<=seconds<=14400: stop("capture duration must be 1..14400 seconds")
    meta=require_backup(bd);run_meta=require_run(rd,bd);epoch=run_meta["epoch"];exp_dir,sent_dir=build_paths(run_meta)
    dev=verify_device(port,meta,hold=True); build_dir=sent_dir if mode=="sentinel" else exp_dir;build_meta=manifest(build_dir,mode,epoch,profile=run_meta["profile"])
    if not out.resolve().is_relative_to(rd.resolve()): stop("capture path must be in private run directory")
    if out.exists() or out.with_name(out.name+".metadata.json").exists(): stop("capture output already exists")
    if mode=="experiment" and (run_meta.get("erased_region",{}).get("status")!="verified" or run_meta.get("experiment_flash",{}).get("status")!="verified"): stop("experiment flash/whole-region erased proof missing")
    out.parent.mkdir(parents=True,exist_ok=True)
    try:
        import serial
    except ImportError:
        stop("pyserial missing (make install)")
    firmware_failure=None
    try:
        start=time.monotonic(); deadline=start+seconds
        ser=serial.Serial(port=None,baudrate=115200,timeout=0.2)
        ser.dtr=False; ser.rts=False; ser.port=port
        with out.open("xb") as f, ser:
            ser.rts=True; time.sleep(0.05); ser.rts=False; time.sleep(0.1)
            last_flush=last_byte=start; tail=b""; pending_line=b""; total=0; guru=0; mount_seen=False; sentinel_ready=False
            while time.monotonic()<deadline:
                now=time.monotonic()
                if total==0 and now-start>CAPTURE_FIRST_BYTE_S:
                    stop(f"no serial bytes in {CAPTURE_FIRST_BYTE_S}s (check USB, port contention, or device left in bootloader)")
                if mode=="sentinel" and not sentinel_ready and now-start>CAPTURE_SENTINEL_READY_S:
                    stop(f"sentinel did not reach H32,SENTINEL_READY within {CAPTURE_SENTINEL_READY_S}s")
                if mode=="experiment" and not mount_seen and now-start>CAPTURE_MOUNT_MILESTONE_S:
                    stop(f"experiment did not log H32,MOUNT within {CAPTURE_MOUNT_MILESTONE_S}s (format hang or firmware fault)")
                if total and now-last_byte>CAPTURE_BYTE_STALL_S:
                    stop(f"serial output stalled for {CAPTURE_BYTE_STALL_S}s")
                chunk=ser.read(4096)
                if chunk:
                    f.write(chunk); total+=len(chunk); last_byte=now
                    tail=(tail+chunk)[-16384:]
                    pending_line,lines=complete_capture_lines(pending_line,chunk)
                    for line in lines:
                        if b"H32,FAIL," in line:
                            firmware_failure=line[line.find(b"H32,FAIL,"):].decode(errors="replace").strip()
                            f.flush();os.fsync(f.fileno())
                            stop("firmware stopped: "+firmware_failure)
                    guru+=chunk.count(b"Guru Meditation")
                    if guru>=CAPTURE_GURU_MAX: stop(f"panic loop ({guru} Guru Meditation errors)")
                    if b"H32,MOUNT," in tail: mount_seen=True
                    if b"H32,SENTINEL_READY," in tail: sentinel_ready=True
                    if b"fault_cycles=90" in tail and b"H32,COMPLETE," in tail:
                        break
                    if time.monotonic()-last_flush>=2:
                        f.flush(); os.fsync(f.fileno()); last_flush=time.monotonic()
            f.flush(); os.fsync(f.fileno())
        if not out.stat().st_size: stop("serial capture empty")
        if mode=="sentinel" and not sentinel_ready: stop("sentinel capture ended without H32,SENTINEL_READY")
        if mode=="experiment" and not mount_seen: stop("experiment capture ended without H32,MOUNT")
    except BaseException as exc:
        if out.exists():
            atomic_json(out.with_name(out.name+".metadata.json"),{"schema":2,"epoch":epoch,"mode":mode,"status":"failed","failure":str(exc),"firmware_failure":firmware_failure,"profile":run_meta["profile"],"effective_config":build_meta["effective_config"],"raw_log_sha256":digest(out),"raw_log_bytes":out.stat().st_size,"device_fingerprint_sha256":dev["fingerprint_sha256"],"source":build_meta["source"],"images":build_meta["images"],"erased_region":run_meta.get("erased_region")},0o600)
        raise
    sent_build=manifest(sent_dir,"sentinel",epoch,profile=run_meta["profile"]);exp_build=manifest(exp_dir,"experiment",epoch,profile=run_meta["profile"])
    public={"schema":2,"status":"captured","epoch":epoch,"profile":run_meta["profile"],"effective_config":build_meta["effective_config"],"port":port,"chip":dev["chip_description"],"flash_bytes":dev["flash_bytes"],
      "device_fingerprint_sha256":dev["fingerprint_sha256"],"git_commit":build_meta["git_commit"],
      "idf_version":build_meta["idf_version"],"fixture_sha256":build_meta["fixture_sha256"],
      "images":build_meta["images"],"mode":mode,"source":build_meta["source"],
      "sentinel_app_sha256":sent_build["images"]["app"]["sha256"],"experiment_app_sha256":exp_build["images"]["app"]["sha256"],
      "original_full_sha256":meta["full"]["sha256"],"original_partition_sha256":meta["partition_table"]["sha256"],
      "original_nvs_sha256":meta["nvs"]["sha256"],"post_sentinel_nvs_sha256":meta.get("post_sentinel_nvs",{}).get("sha256"),
      "erased_region":run_meta.get("erased_region"),"capture_start_reset":"idf_monitor_rts_dtr","test_reset":"esp_restart",
      "raw_log_sha256":digest(out),"raw_log_bytes":out.stat().st_size}
    atomic_json(out.with_name(out.name+".metadata.json"),public,0o600)
def restore(port,bd):
    m=require_backup(bd); verify_device(port,m,hold=True); src=bd/"original-flash.bin"; dst=bd/"restore-readback.bin"
    esptool(port,["--after","no_reset","write_flash","--flash_size","16MB","0",str(src)],1200); esptool(port,["--after","no_reset","read_flash","0",hex(FLASH),str(dst)],1200)
    if dst.stat().st_size!=FLASH or digest(dst)!=m["full"]["sha256"]: stop("full restore readback mismatch")
    if partitions(region(dst,PT_OFF,PT_SIZE))!=m["partition_table"]["entries"] or region(dst,m["nvs"]["offset"],m["nvs"]["bytes"])!=(bd/"original-nvs.bin").read_bytes(): stop("restored partition/NVS identity mismatch")
    for a in m["apps"]:
        b=region(dst,a["offset"],a["size"])
        if dh(b)!=a["partition_sha256"] or app_desc(b)!=a["descriptor"]: stop("restored app identity mismatch: "+a["name"])
    verify_device(port,m,hold=True); proof={"status":"verified","full_sha256":m["full"]["sha256"],"partition_table_sha256":m["partition_table"]["sha256"],"nvs_sha256":m["nvs"]["sha256"],"device_fingerprint_sha256":m["device"]["fingerprint_sha256"]}; atomic_json(bd/"restore-proof-private.json",proof,0o600); print(json.dumps(proof,indent=2))
    esptool(port,["run"],timeout=60)

def kv(s):
    out={}
    for x in s.split(","):
        if "=" not in x: stop("malformed H32 field")
        k,v=x.split("=",1)
        if not k or k in out: stop("duplicate H32 field")
        out[k]=v.strip()
    return out
def validate_runtime_profiles(records,epoch,profile,effective_config,elf_sha256=None):
    cfg=effective_config["settings"]
    mapping={"erase_yield":"CONFIG_SPI_FLASH_YIELD_DURING_ERASE","erase_yield_duration_ms":"CONFIG_SPI_FLASH_ERASE_YIELD_DURATION_MS","erase_yield_ticks":"CONFIG_SPI_FLASH_ERASE_YIELD_TICKS","tick_hz":"CONFIG_FREERTOS_HZ","int_wdt":"CONFIG_ESP_INT_WDT","int_wdt_timeout_ms":"CONFIG_ESP_INT_WDT_TIMEOUT_MS","task_wdt":"CONFIG_ESP_TASK_WDT_EN","task_wdt_init":"CONFIG_ESP_TASK_WDT_INIT","task_wdt_timeout_s":"CONFIG_ESP_TASK_WDT_TIMEOUT_S","fatfs_per_file_cache":"CONFIG_FATFS_PER_FILE_CACHE","fatfs_lfn_heap":"CONFIG_FATFS_LFN_HEAP","fatfs_max_lfn":"CONFIG_FATFS_MAX_LFN","fatfs_timeout_ms":"CONFIG_FATFS_TIMEOUT_MS"}
    for x in records:
        if elf_sha256 and x.get("elf_sha256")!=elf_sha256: stop("runtime ELF hash differs from bound build")
        if x.get("schema")!="2" or x.get("status")!="pass" or x.get("epoch")!=epoch or x.get("profile")!=profile: stop("runtime profile/schema/epoch mismatch")
        for field,key in mapping.items():
            value=cfg.get(key,"n")
            expected="1" if value=="y" else "0" if value=="n" else value
            if x.get(field)!=expected: stop("runtime effective config differs: "+field)
        if x.get("wl_sector_bytes")!="4096": stop("runtime WL sector differs")
def validate_probe_records(ordered,epoch):
    probe=next((x for k,x in ordered if k=="PROBE"),{})
    initial=[x for k,x in ordered if k=="REMOUNT"]
    if [x.get("cycle") for x in initial]!=[str(i) for i in range(1,6)]: stop("initial remount order/count invalid")
    later=[x for k,x in ordered if k=="PROBE_VERIFY"]
    expected=[(p,0,0) for p in (1,2,3)]+[(4,0,0)]+[(4,c,1) for c in range(90)]+[(p,90,0) for p in (5,6)]
    actual=[(int(x.get("phase","-1")),int(x.get("fault_cur","-1")),int(x.get("fault_active","-1"))) for x in later]
    if actual!=expected: stop("retained probe phase/fault identities incomplete/duplicated/out of order")
    last=0; initial_done=False
    for kind,x in ordered:
        if kind=="REMOUNT":
            if initial_done or int(x.get("cycle","0"))!=last+1: stop("initial remount ordering invalid")
            last+=1
        elif kind=="PROBE_VERIFY":
            initial_done=True
            if last!=5 or x.get("epoch")!=epoch or x.get("cycle")!="5" or x.get("identity")!="retained" or x.get("formatted")!="no" or x.get("probe_bytes")!="65536" or x.get("probe_sha256")!=probe.get("sha256") or x.get("verify")!="pass" or not 1<=int(x.get("phase","-1"))<=7: stop("retained probe integrity/phase/order invalid")
def validate_cadence_telemetry(rows):
    for stream in ("opus","pcm"):
        chunks=[x for x in rows["CHUNK"] if x["stream"]==stream]
        summary=next(x for x in rows["CADENCE"] if x["stream"]==stream)
        origin=int(chunks[0]["scheduled_start_us"]);max_late=0
        for i,x in enumerate(chunks):
            if x.get("schema")!="2": stop("cadence telemetry schema mismatch")
            nums={k:int(x.get(k,"-1")) for k in ("open_us","write_us","flush_us","close_us","rename_us","read_verify_us","manifest_us","total_us","scheduled_start_us","actual_start_us","finish_us","lateness_us","finish_lateness_us","max_lateness_us","missed_periods")}
            if any(v<0 for v in nums.values()): stop("missing/negative cadence telemetry")
            scheduled=origin+i*2000000
            if nums["scheduled_start_us"]!=scheduled or nums["actual_start_us"]<scheduled or nums["finish_us"]-nums["actual_start_us"]!=nums["total_us"]: stop("cadence monotonic schedule/service mismatch")
            late=nums["actual_start_us"]-scheduled;max_late=max(max_late,late)
            if nums["lateness_us"]!=late or nums["finish_lateness_us"]!=max(0,nums["finish_us"]-scheduled-2000000) or nums["max_lateness_us"]!=max_late or nums["missed_periods"]!=late//2000000 or int(x.get("backlog","-1"))!=late//2000000: stop("cadence lateness accounting mismatch")
            if sum(nums[k] for k in ("open_us","write_us","flush_us","close_us","rename_us","read_verify_us","manifest_us"))!=nums["total_us"]: stop("cadence operation timing sum mismatch")
            if nums["finish_lateness_us"] or nums["missed_periods"]: stop("absolute cadence deadline missed")
        elapsed=int(summary.get("actual_elapsed_us","-1"))
        if summary.get("schema")!="2" or elapsed<180000000 or elapsed<int(chunks[-1]["finish_us"])-origin or int(summary.get("max_lateness_us","-1"))!=max_late or int(summary.get("max_finish_lateness_us","-1"))!=max(int(x["finish_lateness_us"]) for x in chunks): stop("cadence elapsed/lateness summary mismatch")
        if int(summary.get("watchdog_events","-1"))!=0 or int(summary.get("heartbeat_samples","0"))<=0 or int(summary.get("heartbeat_max_gap_us","0"))<=0: stop("cadence scheduler/watchdog telemetry invalid")
        if int(summary.get("scheduled_deadline_miss","-1"))!=0 or int(summary.get("service_deadline_miss","-1"))!=0: stop("cadence deadline summary mismatch")
        for event in ("start","end"):
            memory=[x for x in rows["MEMORY"] if x.get("workload")==stream and x.get("event")==event]
            if len(memory)!=1 or memory[0].get("schema")!="2" or any(int(memory[0].get(k,"0"))<=0 for k in ("heap","heap_min","heap_largest","storage_stack_bytes","heartbeat_stack_bytes")): stop("cadence memory telemetry missing/invalid")
        service=sum(int(x["total_us"]) for x in chunks)
        if int(summary.get("transaction_us","-1"))!=service: stop("cadence service total mismatch")
        if stream=="pcm" and sum(int(x["bytes"]) for x in chunks)*1000000<=64000*service: stop("durable PCM transaction throughput does not exceed 64000 bytes/s")
        hb=[x for x in rows["HEARTBEAT"] if x.get("workload")==stream]
        if [x.get("event") for x in hb]!=["start","end"] or any(x.get("timer_resolution_us")!="1" or int(x.get("interval_ms","0"))<=0 or int(x.get("priority","-1"))<0 or int(x.get("core","-2")) not in (-1,0,1) for x in hb): stop("heartbeat contract missing/invalid")
        if hb[-1].get("max_gap_us")!=summary["heartbeat_max_gap_us"] or hb[-1].get("samples")!=summary["heartbeat_samples"]: stop("heartbeat summary mismatch")
def parse_log(text,epoch,erased_proof,profile,effective_config,elf_sha256=None):
    rows={}; ordered=[]
    for line in text.splitlines():
        at=line.find("H32,")
        if at<0: continue
        x=re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]","",line[at:]).strip().split(",",2)
        if len(x)!=3: stop("malformed H32 record")
        record=kv(x[2]);rows.setdefault(x[1],[]).append(record);ordered.append((x[1],record))
    if rows.get("FAIL"): stop("firmware reported FAIL")
    req=("SENTINEL","HARDWARE","PARTITION","ERASE_SCAN","RUN_REQUEST","MOUNT","FORMAT","MARKER","RUN","PROBE","REBOOT","REMOUNT","IO","IO_REMOUNT",
         "PROFILE","PROBE_VERIFY","HEARTBEAT","MEMORY","RENAME","DIR_SCAN","CHUNK","CADENCE","CHUNK_REMOUNT","BACKLOG","FAULT","RECOVERY","FLOOR","COMPLETE")
    miss=[x for x in req if not rows.get(x)]
    if miss: stop("incomplete run, missing "+str(miss))
    validate_runtime_profiles(rows["PROFILE"],epoch,profile,effective_config,elf_sha256)
    requests=rows["RUN_REQUEST"]
    if any(x.get("epoch")!=epoch or x.get("mode")!="experiment" or x.get("status")!="pass" for x in requests): stop("firmware requested another epoch/mode")
    if any(x.get("epoch")!=epoch for _,x in ordered): stop("record lacks requested epoch")
    if text.count("Guru Meditation") or "panic\'ed" in text or "capture watchdog" in text: stop("capture contains a panic/watchdog")
    scans=rows["ERASE_SCAN"]
    if scans[0].get("epoch")!=epoch or scans[0].get("all_erased")!="yes" or int(scans[0].get("bytes","0"))!=OUT_SIZE or scans[0].get("sha256")!=erased_proof["sha256"]: stop("firmware full-region scan differs from host proof")
    if any(x.get("epoch")!=epoch or int(x.get("bytes","0"))!=OUT_SIZE or x.get("all_erased")!="no" or not re.fullmatch(r"[0-9a-f]{64}",x.get("sha256","")) for x in scans[1:]): stop("later outbox scans do not preserve expected partition identity")
    runs=rows["RUN"]
    new=[x for x in runs if x.get("event")=="new"]
    if len(new)!=1 or new[0].get("epoch")!=epoch or new[0].get("blank_authority")!="full_scan" or new[0].get("nvs_action")!="reset" or new[0].get("status")!="pass": stop("new-run initialization is missing or unsafe")
    if any(x.get("epoch")!=epoch or x.get("status")!="pass" or (x.get("event")=="resume" and (x.get("blank_authority")!="marker" or x.get("nvs_action")!="preserve")) or x.get("event") not in ("new","resume") for x in runs): stop("resume epoch/marker/NVS contract failed")
    formats=rows["FORMAT"]
    if len(formats)!=1 or formats[0].get("partition")!="outbox" or formats[0].get("status")!="pass" or int(formats[0].get("format_us","-1"))<0: stop("outbox-only format proof missing/duplicated")
    markers=rows["MARKER"]
    if len(markers)!=len(runs) or any(x.get("schema")!="1" or x.get("layout")!="48333201" or x.get("verify")!="pass" for x in markers): stop("marker verification missing/invalid")
    cold=[x for x in rows["MOUNT"] if x.get("kind") in ("cold_after_format","cold_existing")]
    warm=[x for x in rows["MOUNT"] if x.get("kind")=="warm_remount"]
    if len(cold)!=len(runs) or len(warm)!=len(runs) or cold[0].get("kind")!="cold_after_format" or any(x.get("kind")!="cold_existing" or x.get("format_us")!="0" for x in cold[1:]): stop("mount/format counts do not match boots")
    if len(requests)!=len(runs) or len(scans)!=len(runs) or len(rows["SENTINEL"])!=len(runs) or len(rows["PROFILE"])!=len(runs): stop("boot identity/proof counts differ")
    if any(int(x.get("mount_us","-1"))<0 for x in cold+warm): stop("mount timing missing")
    first_run=next(i for i,(k,_) in enumerate(ordered) if k=="RUN")
    if any(x.get("epoch")!=epoch for k,x in ordered[first_run:] if k not in ("FAIL",)): stop("record lacks requested epoch after run initialization")
    boot=[]
    for kind,x in ordered:
        if kind=="RUN_REQUEST":
            if boot: stop("boot ended without RUN authority")
            boot=[kind]
        elif kind in ("SENTINEL","HARDWARE","PARTITION","ERASE_SCAN","FORMAT","MARKER","RUN"):
            if not boot: stop("boot proof appeared before RUN_REQUEST")
            boot.append(kind)
            if kind=="RUN":
                expected=["RUN_REQUEST","SENTINEL","HARDWARE","PARTITION","ERASE_SCAN"]+(["FORMAT"] if x.get("event")=="new" else [])+["MARKER","RUN"]
                if boot!=expected: stop("boot format/marker/authority record order invalid")
                boot=[]
    if boot: stop("incomplete boot authority proof")
    run_ids={x.get("run") for x in runs if x.get("run") is not None}
    if len(run_ids)>1: stop("legacy run identity changed across boots")
    proof_kinds=set(req)-{"SENTINEL","HARDWARE","PARTITION","ERASE_SCAN","RUN_REQUEST","MOUNT","FORMAT","MARKER","RUN"}
    if run_ids and any(r.get("run")!=next(iter(run_ids)) for k,r in ordered if k in proof_kinds and r.get("run") is not None): stop("legacy run identity changed")
    for kind,keys in {"IO":("file",),"IO_REMOUNT":("bytes","index"),"CHUNK":("stream","index"),"CHUNK_REMOUNT":("stream","index"),"CADENCE":("stream",),"BACKLOG":("stream",),"FAULT":("point","cycle"),"RECOVERY":("point","cycle"),"REBOOT":("cycle",),"REMOUNT":("cycle",),"DIR_SCAN":("event",),"FLOOR":("event","step")}.items():
        seen=set()
        for x in rows.get(kind,[]):
            identity=tuple(x.get(k) for k in keys)
            if identity in seen: stop(f"duplicate {kind} record identity {identity}")
            seen.add(identity)
    if any(x.get("status")!="pass" or x.get("mode")!="experiment" or x.get("created")!="no" for x in rows["SENTINEL"]): stop("experiment did not reuse the preserved NVS sentinel")
    if any(int(x.get("flash_bytes","0"))!=FLASH for x in rows["HARDWARE"]): stop("hardware record is not 16 MiB")
    for part in rows["PARTITION"]:
        if int(part.get("factory_offset","0"),0)!=APP_OFF or int(part.get("factory_size","0"),0)!=APP_SIZE or int(part.get("outbox_offset","0"),0)!=OUT_OFF or int(part.get("outbox_size","0"),0)!=OUT_SIZE: stop("running partition record differs from fixture")
    expected_io={f"io-{size}-{i:02d}" for size in (64,192) for i in range(20)}
    if {x.get("file") for x in rows["IO"]}!=expected_io: stop("IO iteration identities incomplete")
    for x in rows["IO"]:
        if not re.fullmatch(r"[0-9a-f]{64}",x.get("sha256","")) or any(int(x.get(k,"-1"))<0 for k in ("open_us","write_us","flush_us","close_us","rename_us","read_verify_us","delete_us","free_before","free_after")): stop("IO timing/checksum fields invalid")
        if x.get("retained")=="no" and x["free_before"]!=x["free_after"]: stop("IO delete did not reclaim space")
    if len(rows["IO"])!=40 or any(x.get("verify")!="pass" for x in rows["IO"]): stop("IO matrix incomplete/failed")
    if sorted(int(x["bytes"]) for x in rows["IO"])!=[65536]*20+[196608]*20: stop("IO shapes incomplete")
    if len(rows["IO_REMOUNT"])!=6 or any(x.get("verify")!="pass" for x in rows["IO_REMOUNT"]): stop("post-remount IO verification incomplete/failed")
    if len(rows["PROBE"])!=1: stop("committed reboot probe creation record missing/duplicated")
    probe=rows["PROBE"][0]
    if probe.get("event")!="created" or probe.get("bytes")!="65536" or probe.get("verify")!="pass" or len(probe.get("sha256",""))!=64: stop("committed reboot probe creation failed")
    if len(rows["REBOOT"])!=5 or {(int(x["cycle"]),x.get("mechanism"),x.get("status")) for x in rows["REBOOT"]}!={(i,"esp_restart","scheduled") for i in range(1,6)}: stop("reboot schedule matrix incomplete/duplicated")
    if len(rows["REMOUNT"])!=5 or {int(x["cycle"]) for x in rows["REMOUNT"]}!=set(range(1,6)): stop("verified remount matrix incomplete/duplicated")
    if any(x.get("formatted")!="no" or x.get("probe_bytes")!="65536" or x.get("probe_sha256")!=probe["sha256"] or x.get("verify")!="pass" for x in rows["REMOUNT"]): stop("committed probe did not survive every remount")
    validate_probe_records(ordered,epoch)
    if len(rows["RENAME"])!=1: stop("replace-existing rename result missing/duplicated")
    rename=rows["RENAME"][0]
    replaced=(rename.get("outcome")=="replaced" and rename.get("errno")=="0" and rename.get("old_preserved")=="no" and rename.get("new_valid")=="yes" and rename.get("part_present")=="no")
    refused=(rename.get("outcome")=="not_supported" and int(rename.get("errno","0"))!=0 and rename.get("old_preserved")=="yes" and rename.get("new_valid")=="no" and rename.get("part_present")=="yes")
    if rename.get("case")!="replace_existing" or rename.get("same_directory")!="yes" or rename.get("status")!="pass" or not (replaced or refused): stop("unsafe or ambiguous final-file replacement result")
    if len(rows["DIR_SCAN"])!=2: stop("stale-part directory scans missing/duplicated")
    scans={x.get("event"):x for x in rows["DIR_SCAN"]}
    before=scans.get("before_cleanup",{});after=scans.get("after_cleanup",{})
    if before.get("case")!="stale_part" or before.get("stale_seen")!="1" or before.get("final_seen")!="yes" or before.get("final_valid")!="yes" or before.get("removed")!="1": stop("stale-part cleanup was not enumerated and applied")
    if after.get("case")!="stale_part" or after.get("stale_seen")!="0" or after.get("final_seen")!="yes" or after.get("final_valid")!="yes" or after.get("removed")!="0": stop("post-cleanup directory state is invalid")
    if any(x.get("unknown_removed")!="0" or x.get("prior_valid")!="yes" or x.get("status")!="pass" for x in rows["DIR_SCAN"]): stop("directory cleanup damaged or removed an unknown file")
    if len(rows["CADENCE"])!=2: stop("cadence record count invalid")
    cad={(x.get("stream"),int(x.get("seconds","0")),int(x.get("rate","0"))):x for x in rows["CADENCE"]}
    for shape in (("opus",180,2000),("pcm",180,67200)):
        if shape not in cad or int(cad[shape].get("deadline_miss","-1")) or int(cad[shape].get("backlog_high","-1")): stop("cadence incomplete/failed")
    if len(rows["CHUNK"])!=180 or {(x.get("stream"),int(x["index"])) for x in rows["CHUNK"]}!={(s,i) for s in ("opus","pcm") for i in range(90)}: stop("chunk cadence matrix incomplete/duplicated")
    for stream,rate in (("opus",2000),("pcm",67200)):
        chunks=[x for x in rows["CHUNK"] if x.get("stream")==stream]
        if [int(x["index"]) for x in chunks]!=list(range(90)): stop("chunk order invalid")
        if any(int(x.get("bytes","0"))!=rate*2 or int(x.get("backlog","-1"))!=0 or int(x.get("total_us","-1")) not in range(2000000) or not re.fullmatch(r"[0-9a-f]{64}",x.get("sha256","")) or any(int(x.get(k,"-1"))<=0 for k in ("heap","stack_words","free")) for x in chunks): stop("chunk size/deadline/checksum/memory failed")
        summary=cad[(stream,180,rate)]
        aggregate=dh("".join(x["sha256"] for x in chunks).encode())
        if summary.get("aggregate_kind")!="ordered_chunk_sha256_hex" or summary.get("aggregate_sha256")!=aggregate or summary.get("chunks")!="90" or summary.get("chunk_seconds")!="2": stop("cadence aggregate/count proof failed")
    validate_cadence_telemetry(rows)
    if len(rows["CHUNK_REMOUNT"])!=6 or any(x.get("verify")!="pass" for x in rows["CHUNK_REMOUNT"]): stop("post-remount chunk verification incomplete/failed")
    if {(x.get("stream"),x.get("validated"),x.get("reclaimed"),x.get("retained")) for x in rows["BACKLOG"]}!={("opus","90","87","3"),("pcm","90","87","3")}: stop("backlog reclamation evidence incomplete")
    if len(rows["FAULT"])!=90 or {(int(x["point"]),int(x["cycle"])) for x in rows["FAULT"]}!={(p,c) for p in range(9) for c in range(10)}: stop("fault matrix incomplete/duplicated")
    fault_shapes={
      0:("after_buffered_128_before_flush",128,"no","no","no","no","no"),
      1:("after_buffered_half_before_flush",32768,"no","no","no","no","no"),
      2:("after_buffered_65535_before_flush",65535,"no","no","no","no","no"),
      3:("after_fsync_before_close",65536,"yes","no","no","no","no"),
      4:("after_close_before_rename",65536,"yes","yes","no","no","no"),
      5:("after_rename_before_metadata",65536,"yes","yes","yes","no","no"),
      6:("after_metadata_before_delete",65536,"yes","yes","yes","yes","no"),
      7:("after_final_delete_before_manifest_delete",65536,"yes","yes","yes","yes","yes"),
      8:("after_delete",65536,"yes","yes","yes","yes","yes")}
    for x in rows["FAULT"]:
        shape=(x.get("boundary"),int(x.get("bytes_written","-1")),x.get("flushed"),x.get("closed"),x.get("renamed"),x.get("metadata_committed"),x.get("delete_attempted"))
        if x.get("mechanism")!="esp_restart" or x.get("fault_model")!="software_reset_no_power_cut" or shape!=fault_shapes[int(x["point"])]: stop("fault mechanism/boundary evidence is inaccurate")
    if len(rows["RECOVERY"])!=90 or any(x.get("status")!="pass" or x.get("prior_valid")!="yes" for x in rows["RECOVERY"]): stop("recovery matrix incomplete/failed")
    pending=None; recovered=0
    for kind,x in ordered:
        if kind=="FAULT":
            identity=(int(x["point"]),int(x["cycle"]))
            if pending is not None or identity!=(recovered//10,recovered%10): stop("fault cursor advanced before recovery")
            pending=identity
        elif kind=="RECOVERY":
            identity=(int(x["point"]),int(x["cycle"]))
            if identity!=pending: stop("recovery lacks matching preceding fault")
            point=identity[0]
            expected_final="present" if point in (5,6) else "absent"
            expected_manifest="present" if point in (6,7) else "absent"
            if x.get("final")!=expected_final or x.get("manifest")!=expected_manifest or x.get("final_valid")!=("yes" if point in (5,6) else "no"): stop("recovery accepted an invalid final artifact")
            pending=None; recovered+=1
    if pending is not None or recovered!=90: stop("recovery order incomplete")
    floor={(x.get("event"),x.get("status")) for x in rows["FLOOR"]}
    if not {("admission","pass"),("post_remount","pass"),("reclaim","pass")}<=floor: stop("floor admission/remount/reclaim failed")
    deriv=[x for x in rows["FLOOR"] if x.get("event")=="derivation"]
    fills=[x for x in rows["FLOOR"] if x.get("event")=="fill"]
    admissions=[x for x in rows["FLOOR"] if x.get("event")=="admission"]
    post=[x for x in rows["FLOOR"] if x.get("event")=="post_remount"]
    reclaim=[x for x in rows["FLOOR"] if x.get("event")=="reclaim"]
    if len(deriv)!=1 or len(admissions)!=1 or len(post)!=1 or len(reclaim)!=1 or not fills: stop("safe-floor event counts are invalid")
    configured=int(deriv[0].get("configured","0"));request_cost=int(deriv[0].get("request_cost","0"))
    if configured<=0 or int(deriv[0].get("allocation_unit","0"))!=4096 or int(deriv[0].get("measured_max_overhead","-1"))<0 or request_cost<=int(deriv[0].get("largest_chunk","0")): stop("safe-floor derivation is incomplete")
    if [int(x.get("step","-1")) for x in fills]!=list(range(len(fills))): stop("fill steps are not contiguous")
    if any(int(x.get("configured","0"))!=configured or x.get("floor_held")!="yes" or x.get("status")!="pass" or int(x.get("free_after","0"))<configured or int(x.get("allocated","-1"))<int(x.get("payload","0")) for x in fills): stop("a fill write crossed the safe floor")
    admission=admissions[0]
    if int(admission.get("configured","0"))!=configured or int(admission.get("request_cost","0"))!=request_cost or admission.get("decision")!="reject" or admission.get("prior_valid")!="yes" or not (configured<=int(admission.get("free","0"))<configured+request_cost) or int(admission.get("post_write_min","0"))<configured: stop("admission proof does not preserve the safe floor")
    if int(post[0].get("configured","0"))!=configured or post[0].get("floor_held")!="yes" or int(post[0].get("free","0"))<configured or post[0].get("prior_valid")!="yes" or int(post[0].get("actual","-1"))!=int(post[0].get("expected","-2")): stop("post-remount floor/files proof failed")
    if int(reclaim[0].get("configured","0"))!=configured or reclaim[0].get("residual_fill_files")!="0": stop("fill reclaim left residual files")
    io_hashes={(x["bytes"],str(int(x["file"].rsplit("-",1)[1]))):x["sha256"] for x in rows["IO"]}
    if {(x.get("bytes"),x.get("index")) for x in rows["IO_REMOUNT"]}!={(str(b),str(i)) for b in (65536,196608) for i in (0,10,19)} or any(x.get("sha256")!=io_hashes[(x["bytes"],x["index"])] for x in rows["IO_REMOUNT"]): stop("IO remount checksum identities differ")
    chunk_hashes={(x["stream"],x["index"]):x["sha256"] for x in rows["CHUNK"]}
    if {(x.get("stream"),x.get("index")) for x in rows["CHUNK_REMOUNT"]}!={(s,str(i)) for s in ("opus","pcm") for i in (0,45,89)} or any(x.get("sha256")!=chunk_hashes[(x["stream"],x["index"])] for x in rows["CHUNK_REMOUNT"]): stop("chunk remount checksum identities differ")
    completed=[x for x in rows["COMPLETE"] if x.get("fault_cycles")=="90"]
    if len(completed)!=1 or int(completed[0].get("safe_floor","0"))!=configured: stop("detailed completion record missing or floor differs")
    return rows
def validate_sentinel(inp,bd,rd,historical=False,persist_proof=True):
    if historical: persist_proof=False
    run_meta=require_run(rd,bd);epoch=run_meta["epoch"];profile=run_meta["profile"]
    _,sent_dir=build_paths(run_meta)
    rows={}
    for line in inp.read_text(errors="replace").splitlines():
        at=line.find("H32,")
        if at<0: continue
        x=re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]","",line[at:]).strip().split(",",2)
        if len(x)==3: rows.setdefault(x[1],[]).append(kv(x[2]))
    if rows.get("FAIL"): stop("sentinel firmware reported FAIL")
    sent=rows.get("SENTINEL",[]); ready=rows.get("SENTINEL_READY",[])
    if not sent or sent[-1].get("mode")!="sentinel_only" or sent[-1].get("status")!="pass": stop("valid sentinel record missing")
    if not ready or ready[-1].get("storage_access")!="no" or ready[-1].get("action")!="halt": stop("sentinel-only halt record missing")
    metadata=json.loads(inp.with_name(inp.name+".metadata.json").read_text());backup_meta=require_backup(bd)
    sent_build=manifest(sent_dir,"sentinel",epoch,profile=profile,historical=historical)
    validate_runtime_profiles(rows.get("PROFILE",[]),epoch,profile,sent_build["effective_config"],sent_build["app_descriptor"]["elf_sha256"])
    if not rows.get("PROFILE"): stop("sentinel runtime profile missing")
    if metadata.get("source")!=sent_build["source"] or metadata.get("images")!=sent_build["images"] or metadata.get("profile")!=profile or metadata.get("effective_config")!=sent_build["effective_config"]: stop("sentinel capture profile/config mismatch")
    if metadata.get("epoch")!=epoch or metadata.get("raw_log_sha256")!=digest(inp) or metadata.get("raw_log_bytes")!=inp.stat().st_size or metadata.get("mode")!="sentinel" or metadata.get("device_fingerprint_sha256")!=backup_meta["device"]["fingerprint_sha256"] or metadata.get("images",{}).get("app",{}).get("sha256")!=sent_build["images"]["app"]["sha256"]: stop("sentinel log metadata is not bound to backup/build")
    if any(x.get("epoch")!=epoch for records in rows.values() for x in records): stop("sentinel records epoch mismatch")
    proof={"epoch":epoch,"status":"verified","mode":"sentinel_only","storage_access":"no","source_log_path":str(inp.resolve()),"source_log_sha256":digest(inp),
      "original_full_sha256":backup_meta["full"]["sha256"],"original_nvs_sha256":backup_meta["nvs"]["sha256"],
      "device_fingerprint_sha256":backup_meta["device"]["fingerprint_sha256"],"sentinel_app_sha256":sent_build["images"]["app"]["sha256"]}
    if persist_proof: atomic_json(bd/"sentinel-proof-private.json",proof,0o600)
    print(json.dumps(proof,indent=2)); return proof
def write_csv(path,fields,records):
    fd,tmp=tempfile.mkstemp(dir=path.parent,prefix="."+path.name)
    with os.fdopen(fd,"w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields,extrasaction="ignore",lineterminator="\n"); w.writeheader(); w.writerows(records)
    os.replace(tmp,path)
def aggregate(inp,out,rd):
    side=inp.with_name(inp.name+".metadata.json")
    try: metadata=json.loads(side.read_text())
    except Exception as e: stop(f"capture metadata missing/invalid: {e}")
    run_meta=require_run(rd);epoch=run_meta["epoch"];exp_dir,sent_dir=build_paths(run_meta)
    if metadata.get("schema")!=2 or metadata.get("status")!="captured" or metadata.get("epoch")!=epoch or metadata.get("mode")!="experiment" or metadata.get("flash_bytes")!=FLASH or metadata.get("capture_start_reset")!="idf_monitor_rts_dtr" or metadata.get("test_reset")!="esp_restart"  or metadata.get("raw_log_sha256")!=digest(inp) or metadata.get("raw_log_bytes")!=inp.stat().st_size: stop("capture metadata does not describe the current qualified run")
    if metadata.get("erased_region")!=run_meta.get("erased_region") or metadata.get("erased_region",{}).get("status")!="verified" or metadata["erased_region"].get("bytes")!=OUT_SIZE or metadata["erased_region"].get("offset")!=OUT_OFF or metadata["erased_region"].get("sha256")!=ERASED_SHA256: stop("capture lacks complete erased-region proof")
    if metadata.get("original_partition_sha256")!=run_meta.get("original_partition_sha256") or metadata.get("original_nvs_sha256")!=run_meta.get("original_nvs_sha256"): stop("capture partition/NVS backup bindings differ")
    if metadata.get("original_full_sha256")!=run_meta["original_full_sha256"] or metadata.get("device_fingerprint_sha256")!=run_meta["device_fingerprint_sha256"]: stop("capture is bound to another backup/device")
    if run_meta.get("experiment_flash",{}).get("status")!="verified" or metadata.get("post_sentinel_nvs_sha256")!=run_meta["experiment_flash"].get("post_flash_nvs_sha256"): stop("capture post-sentinel NVS binding differs")
    exp=manifest(exp_dir,"experiment",epoch,profile=run_meta["profile"],historical=True);sent=manifest(sent_dir,"sentinel",epoch,profile=run_meta["profile"],historical=True)
    if metadata.get("experiment_app_sha256")!=exp["images"]["app"]["sha256"] or metadata.get("sentinel_app_sha256")!=sent["images"]["app"]["sha256"] or metadata.get("images")!=exp["images"]: stop("capture build hashes differ from manifests")
    if not inp.resolve().is_relative_to(rd.resolve()): stop("capture log must be in the private run directory")
    if out.exists() and (not out.is_dir() or any(out.iterdir())): stop("evidence staging directory must be empty")
    if metadata.get("source")!=exp["source"] or metadata.get("profile")!=run_meta["profile"] or metadata.get("effective_config")!=exp["effective_config"]: stop("capture profile/config binding differs")
    r=parse_log(inp.read_text(errors="replace"),epoch,metadata["erased_region"],run_meta["profile"],exp["effective_config"],exp["app_descriptor"]["elf_sha256"])
    out.parent.mkdir(parents=True,exist_ok=True)
    stage=Path(tempfile.mkdtemp(prefix="."+out.name+"-",dir=out.parent))
    final_out=out;out=stage
    write_csv(out/"io-measurements.csv",["status","test","size_bytes","iteration","open_us","write_us","flush_us","close_us","rename_us","read_verify_us","delete_us","free_before","free_after","verify","sha256"],({"status":"measured","test":x["file"],"size_bytes":x["bytes"],"iteration":i,**x} for i,x in enumerate(r["IO"])))
    write_csv(out/"cadence-results.csv",["status","test","seconds","bytes_per_second","max_write_us","max_flush_us","max_rename_us","max_manifest_us","missed_deadlines","backlog_high","min_free_bytes","min_heap_bytes","min_psram","min_stack_words","aggregate_kind","aggregate_sha256"],({"status":"measured","test":x["stream"],"bytes_per_second":x["rate"],"missed_deadlines":x["deadline_miss"],"min_free_bytes":x["min_free"],"min_heap_bytes":x["min_heap"],**x} for x in r["CADENCE"]))
    write_csv(out/"chunk-timings.csv",["schema","epoch","stream","index","bytes","open_us","write_us","flush_us","close_us","rename_us","read_verify_us","manifest_us","total_us","scheduled_start_us","actual_start_us","finish_us","lateness_us","finish_lateness_us","max_lateness_us","missed_periods","backlog","free","heap","psram","stack_words","sha256"],r["CHUNK"])
    atomic_json(out/"scheduler-telemetry.json",{"heartbeat":r["HEARTBEAT"],"memory":r["MEMORY"],"cadence":r["CADENCE"]})
    rec={(x["point"],x["cycle"]):x for x in r["RECOVERY"]}
    write_csv(out/"interruption-results.csv",["status","reset_class","point","cycle","prior_files_valid","partial_accepted","part","final","manifest","final_valid"],({"status":"measured","reset_class":"esp_restart","point":x["point"],"cycle":x["cycle"],"prior_files_valid":rec[(x["point"],x["cycle"])]["prior_valid"],"partial_accepted":"no",**rec[(x["point"],x["cycle"])]} for x in r["FAULT"]))
    complete=next(x for x in r["COMPLETE"] if x.get("fault_cycles")=="90")
    safe_metadata={k:metadata[k] for k in ("schema","epoch","profile","effective_config","flash_bytes","git_commit","idf_version","fixture_sha256","images","source","sentinel_app_sha256","experiment_app_sha256","erased_region","capture_start_reset","test_reset","raw_log_sha256","raw_log_bytes")}
    summary={"status":"measured","run_metadata":safe_metadata,"record_counts":{k:len(v) for k,v in sorted(r.items())},"filesystem_first":r["MOUNT"][0],"filesystem_last":r["MOUNT"][-1],"safe_floor_events":r["FLOOR"],"completion":complete,"power_loss":"unproven","source_log_sha256":digest(inp)}; atomic_json(out/"run-summary.json",summary)
    if final_out.exists(): final_out.rmdir()
    os.replace(stage,final_out)
    print(json.dumps(summary,indent=2))

def host_checks(inp=None,bd=None,rd=None):
    import copy
    epoch="1"*32; hash_value="a"*64
    probe={"epoch":epoch,"sha256":hash_value}
    initial=[("REMOUNT",{"epoch":epoch,"cycle":str(i)}) for i in range(1,6)]
    later={"epoch":epoch,"cycle":"5","identity":"retained","formatted":"no","probe_bytes":"65536","probe_sha256":hash_value,"verify":"pass","phase":"1"}
    shapes=[(p,0,0) for p in (1,2,3)]+[(4,0,0)]+[(4,c,1) for c in range(90)]+[(p,90,0) for p in (5,6)]
    valid=[("PROBE",probe),*initial,*[("PROBE_VERIFY",{**later,"phase":str(p),"fault_cur":str(c),"fault_active":str(a)}) for p,c,a in shapes]]
    validate_probe_records(valid,epoch)
    rejected=[]
    cases={"duplicate_initial":valid+[initial[-1]],"missing_initial":valid[:3]+valid[4:],"wrong_epoch":copy.deepcopy(valid),"later_before_initial":valid[:1]+valid[-1:]+valid[1:-1],"duplicate_later":valid+[valid[-1]],"missing_later":valid[:-1]}
    cases["wrong_epoch"][-1][1]["epoch"]="2"*32
    for name,records in cases.items():
        try: validate_probe_records(records,epoch)
        except SystemExit: rejected.append(name)
        else: stop("host shape check accepted "+name)
    pending,lines=complete_capture_lines(b"",b"H32,FA")
    if lines: stop("partial failure emitted early")
    pending,lines=complete_capture_lines(pending,b"IL,epoch="+epoch.encode()+b",stage=pcm,reason=cadence")
    if lines: stop("unterminated failure emitted early")
    pending,lines=complete_capture_lines(pending,b"\r\n")
    if pending or len(lines)!=1 or not lines[0].startswith(b"H32,FAIL,"): stop("complete fragmented failure was not detected")
    with tempfile.TemporaryDirectory() as d:
        path=Path(d)/"records.csv";write_csv(path,["x"],[{"x":"yes"}])
        if path.read_bytes()!=b"x\nyes\n": stop("CSV line termination check failed")
    cli_regression="not requested"
    if any(x is not None for x in (inp,bd,rd)):
        if not all(x is not None for x in (inp,bd,rd)): stop("CLI regression requires --input, --backup-dir and --run-dir")
        import sys
        proof_path=bd/"sentinel-proof-private.json"
        proof_before=proof_path.read_bytes() if proof_path.exists() else None
        result=subprocess.run([sys.executable,str(Path(__file__).resolve()),"validate-sentinel","--historical-validation","--input",str(inp),"--backup-dir",str(bd),"--run-dir",str(rd)],capture_output=True,text=True)
        if result.returncode: stop("fresh interpreter sentinel CLI regression failed: "+result.stderr.strip())
        proof=json.loads(result.stdout);run_meta=json.loads((rd/"operator-run-private.json").read_text())
        metadata=json.loads(inp.with_name(inp.name+".metadata.json").read_text())
        if proof.get("epoch")!=run_meta["epoch"] or proof.get("sentinel_app_sha256")!=metadata["images"]["app"]["sha256"] or proof.get("source_log_sha256")!=digest(inp): stop("fresh interpreter selected incorrect epoch/image/capture")
        if (proof_path.read_bytes() if proof_path.exists() else None)!=proof_before: stop("historical CLI mutated sentinel proof")
        cli_regression="passed, archived epoch/image/capture bound; mutation proof unchanged"
    print(json.dumps({"status":"pass","scope":"synthetic structural checks and optional captured-evidence CLI regression; no hardware mutation","rejected":rejected,"csv":"LF","sentinel_cli":cli_regression},indent=2))

def main():
    p=argparse.ArgumentParser(); p.add_argument("command",choices=["inventory","backup","prepare-run","build","validate-build","sentinel-flash","validate-sentinel","sentinel-backup","experiment-flash","capture","parse","restore","host-checks"]); p.add_argument("--historical-validation",action="store_true",help="read-only archived sentinel validation; does not authorize flashing");p.add_argument("--profile",choices=PROFILES); p.add_argument("--port"); p.add_argument("--backup-dir",type=Path);p.add_argument("--run-dir",type=Path); p.add_argument("--output",type=Path); p.add_argument("--input",type=Path); p.add_argument("--evidence-dir",type=Path); p.add_argument("--seconds",type=int,default=None);p.add_argument("--mode",choices=["sentinel","experiment"],default="experiment"); a=p.parse_args(); port=None if a.command in ("prepare-run","build","validate-build","validate-sentinel","parse","host-checks") else one_port(a.port)
    if a.historical_validation and a.command!="validate-sentinel": p.error("--historical-validation only applies to read-only validate-sentinel")
    if a.command!="prepare-run" and a.profile and a.run_dir and require_run(a.run_dir)["profile"]!=a.profile: p.error("--profile differs from immutable prepared run")
    if a.command=="host-checks": host_checks(a.input,a.backup_dir,a.run_dir)
    elif a.command=="inventory": inventory(port)
    elif a.command=="backup": backup(port,a.backup_dir or Path("/private/tmp")/f"family-link-h32-{int(time.time())}")
    elif a.command=="prepare-run":
        if not a.backup_dir or not a.run_dir: p.error("--backup-dir and --run-dir required")
        if not a.profile: p.error("--profile required for immutable run selection")
        prepare_run(a.run_dir,a.backup_dir,a.profile)
    elif a.command=="build":
        run_meta=require_run(a.run_dir); build(run_meta["epoch"],run_meta["profile"])
    elif a.command=="validate-build":
        run_meta=require_run(a.run_dir);epoch=run_meta["epoch"];exp_dir,sent_dir=build_paths(run_meta)
        print(json.dumps({"experiment":manifest(exp_dir,"experiment",epoch,profile=run_meta["profile"]),"sentinel":manifest(sent_dir,"sentinel",epoch,profile=run_meta["profile"])},indent=2))
    elif a.command in ("sentinel-flash","experiment-flash"):
        if not a.backup_dir: p.error("--backup-dir required")
        flash(port,a.backup_dir,a.command=="sentinel-flash",a.run_dir)
    elif a.command=="validate-sentinel":
        if not a.input or not a.backup_dir: p.error("--input and --backup-dir required")
        validate_sentinel(a.input,a.backup_dir,a.run_dir,historical=a.historical_validation,persist_proof=not a.historical_validation)
    elif a.command=="sentinel-backup":
        if not a.backup_dir: p.error("--backup-dir required")
        sentinel_backup(port,a.backup_dir,a.run_dir)
    elif a.command=="capture":
        if not a.backup_dir: p.error("--backup-dir required")
        seconds=a.seconds if a.seconds is not None else (CAPTURE_SENTINEL_DEFAULT_S if a.mode=="sentinel" else CAPTURE_EXPERIMENT_DEFAULT_S)
        capture(port,a.backup_dir,a.run_dir,a.output or a.run_dir/"h32-capture.txt",seconds,a.mode)
    elif a.command=="parse":
        if not a.input or not a.evidence_dir: p.error("--input and --evidence-dir required")
        aggregate(a.input,a.evidence_dir,a.run_dir)
    elif a.command=="restore":
        if not a.backup_dir: p.error("--backup-dir required")
        restore(port,a.backup_dir)
if __name__=="__main__": main()
