#!/usr/bin/env python3
"""Offline synthetic checks for the H38 v1 contract. Prints no private values."""
from __future__ import annotations

import base64
import copy
import hashlib
import importlib
import json
import pathlib
import struct
import sys

import h38_sd_contract as c


def check(condition: bool, label: str) -> None:
    if not condition:
        raise AssertionError(label)


def must_fail(fn, label: str) -> None:
    try:
        fn()
    except (c.ContractError, ValueError, TypeError):
        return
    raise AssertionError(label)


def edit_event(raw: bytes, event: str, old: bytes, new: bytes, occurrence: int = 0) -> bytes:
    lines=raw.splitlines(keepends=True)
    seen=0
    for index,line in enumerate(lines):
        if line.startswith(f"H38,1,{event},".encode()) and old in line:
            if seen == occurrence:
                lines[index]=line.replace(old,new,1)
                return b"".join(lines)
            seen+=1
    raise AssertionError("fixture mutation target missing")


def intent() -> dict:
    value = {
        "schema":"h38-intent-v1", "epoch":"1234567890abcdef1234567890abcdef",
        "profile":c.PROFILE,"exit_state":c.EXIT_STATE,
        "h35_reference_epoch":c.H35_REFERENCE_EPOCH,"private_cid_sha256":"a"*64,
        "old_mbr_sha256":"b"*64,"card_sector_bytes":512,"card_sector_count":c.REFERENCE_SDHC32_SECTORS,
        "volume_start_lba":c.VOLUME_START,"volume_sector_count":c.VOLUME_SECTORS,"mbr_bytes":512,
        "format":copy.deepcopy(c.FORMAT),"io":copy.deepcopy(c.IO),
        "capture_limits":copy.deepcopy(c.CAPTURE_LIMITS),
        "source_sdk_snapshot":{"source_revision":"c"*40,"sdk_version":"5.4.2"},
    }
    value["private_cid_sha256"] = c.h35_cid_digest(c.H35_REFERENCE_EPOCH, {
        "mfg_id":1,"oem_id":1,"revision":1,"serial":1,"date":1,"name_size":0,"name_hex":""})
    return value


def bpb_sector() -> bytes:
    b=bytearray(512)
    b[0:3]=b"\xeb\x58\x90"; b[3:11]=b"MSDOS5.0"
    struct.pack_into("<H",b,11,512); b[13]=8; struct.pack_into("<H",b,14,32); b[16]=2
    struct.pack_into("<H",b,17,0); struct.pack_into("<H",b,19,0); struct.pack_into("<H",b,22,0)
    struct.pack_into("<I",b,32,c.VOLUME_SECTORS); struct.pack_into("<I",b,36,1025)
    struct.pack_into("<I",b,44,2); struct.pack_into("<H",b,48,1); struct.pack_into("<H",b,50,6)
    b[510:512]=b"\x55\xaa"
    return bytes(b)


def run() -> int:
    passes=0
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
    check(importlib.import_module("scripts.h38_sd_contract").PROFILE == c.PROFILE,
          "package import failed"); passes+=1
    i=intent(); raw=c.canonical_intent_bytes(i)
    check(c.load_intent(raw)==i,"canonical intent roundtrip"); passes+=1
    check(len(c.intent_sha256(i))==64,"intent digest shape"); passes+=1
    bad=copy.deepcopy(i); bad["card_sector_count"]=True
    must_fail(lambda:c.validate_intent(bad),"bool accepted as integer"); passes+=1
    bad=copy.deepcopy(i); bad["io"]["time_limit_ms"]=900001
    must_fail(lambda:c.validate_intent(bad),"altered nested limit accepted"); passes+=1
    bad=copy.deepcopy(i); bad["extra"]=1
    must_fail(lambda:c.validate_intent(bad),"extra intent key accepted"); passes+=1
    must_fail(lambda:c.load_intent(b'{"x":1,"x":2}'),"duplicate JSON key accepted"); passes+=1
    must_fail(lambda:c.load_intent(raw+b" "),"noncanonical whitespace intent accepted"); passes+=1

    cmd=c.bind_command(i,"d"*64)
    check(cmd.endswith(b"\n") and len(cmd)<=512,"BIND command bound"); passes+=1
    commands=[cmd]+[c.phase_command(p,i["epoch"],"d"*64) for p in ("LAYOUT","FORMAT","IO","FINISH")]
    c.validate_command_sequence(commands,i,"d"*64); passes+=1
    bad_commands=list(commands); bad_commands[2]=bad_commands[2].replace(b",2\n",b",9\n")
    must_fail(lambda:c.validate_command_sequence(bad_commands,i,"d"*64),"bad command sequence accepted"); passes+=1
    bad_commands=list(commands); bad_commands.append(commands[-1])
    must_fail(lambda:c.validate_command_sequence(bad_commands,i,"d"*64),"extra command accepted"); passes+=1
    must_fail(lambda:c.bind_command(i,"D"*64),"uppercase ELF digest accepted"); passes+=1
    m=c.build_mbr(i["epoch"]); c.validate_mbr(m,i["epoch"],i["card_sector_count"])
    check(len(m)==512 and m[510:]==b"\x55\xaa","canonical MBR built"); passes+=1
    for pos in (446,450,454,458,510):
        broken=bytearray(m); broken[pos]^=1
        must_fail(lambda broken=bytes(broken):c.validate_mbr(broken,i["epoch"]),"mutated MBR accepted")
        passes+=1
    bpb=bpb_sector(); c.validate_bpb(bpb); passes+=1
    badb=bytearray(bpb); struct.pack_into("<I",badb,36,1024)
    must_fail(lambda:c.validate_bpb(bytes(badb)),"wrong FAT size BPB accepted"); passes+=1

    # Parser negative fixtures exercise exact field order, identity binding and partial capture.
    epoch=i["epoch"]; elf="d"*64
    def record(event: str, fields: dict[str,str]) -> bytes:
        fields={"epoch":epoch,"elf_sha256":elf,**fields}
        return ("H38,1,"+event+","+",".join(f"{k}={v}" for k,v in fields.items())+"\n").encode("ascii")
    tr={k:v for k,v in c.TRANSPORT.items()}
    partial=record("BOOT",{"reset_reason":"1"})
    must_fail(lambda:c.parse_capture(partial,epoch,elf),"partial capture accepted"); passes+=1
    malformed=partial.replace(b"epoch=",b"elf_sha256=",1)
    must_fail(lambda:c.parse_capture(malformed,epoch,elf),"reordered/duplicate field accepted"); passes+=1
    must_fail(lambda:c.parse_capture(record("BOOT",{"reset_reason":"1"}),"0"*32,elf),"wrong epoch accepted"); passes+=1

    # A fully structured synthetic success fixture exercises success gates.
    rows=[]
    add=lambda event, fields: rows.append(record(event,fields))
    add("BOOT",{"reset_reason":"1"}); add("TRANSPORT",tr)
    add("HOST",{"error":"0"}); add("SLOT",{"error":"0"}); add("CARD",{"error":"0"})
    add("GEOMETRY",c.geometry_record_fields(c.REFERENCE_SDHC32_SECTORS))
    add("CID_PRIVATE",{"mfg_id":"1","oem_id":"1","revision":"1","serial":"1","date":"1","name_size":"0","name_hex":""})
    add("READY",{"accepts":"BIND"}); add("IDENTITY_MATCH",{"reference_epoch":c.H35_REFERENCE_EPOCH,"match":"1","error":"0"})
    add("LAYOUT_RESULT",{"physical_lba":"0","write_sectors":"1","write_bytes":"512","write_count":"1","readback_match":"1","readback_sha256":hashlib.sha256(m).hexdigest(),"readback_base64_private":base64.b64encode(m).decode(),"trim_requests":"0","erase_calls":"0","status":"ok","error":"0"})
    add("FORMAT_START",{"volume_sectors":"1048576","sector_bytes":"512","fat_type":"FAT32","fat_count":"2","allocation_unit_bytes":"4096","format_flags":"10","work_buffer_bytes":"4096","write_limit_bytes":"4194304","read_limit_bytes":"16777216","time_limit_ms":"120000"})
    add("FORMAT_RESULT",{"f_result":"0","volume_sectors":"1048576","sector_bytes":"512","fat_type":"FAT32","fat_count":"2","allocation_unit_bytes":"4096","cluster_count":"130811","fat_sectors":"1025","root_cluster_sectors":"8","read_bytes":"4096","write_bytes":"1055744","write_calls":"263","max_call_sectors":"8","trim_requests":"1","erase_calls":"0","bpb_valid":"1","bpb_sha256":hashlib.sha256(bpb).hexdigest(),"bpb_base64_private":base64.b64encode(bpb).decode(),"elapsed_us":"1","status":"ok","error":"0"})
    add("MOUNT_RESULT",{"kind":"initial","cycle":"0","mounted":"1","fs_type":"3","sector_bytes":"512","volume_sectors":"1048576","allocation_unit_bytes":"4096","cluster_count":"130811","total_bytes":"535801856","free_bytes":"499998720","read_bytes":"512","write_bytes":"0","elapsed_us":"1","status":"ok","error":"0"})
    keep=[0,10,19,20,30,39]
    hashes={n:c._pattern_sha256(65536 if n<20 else 196608, 1048576+(65536 if n<20 else 196608)+(n if n<20 else n-20)) for n in range(40)}
    for n in range(40):
        size=65536 if n<20 else 196608; it=n if n<20 else n-20; retained=n in keep
        add("IO_RESULT",{"index":str(n),"size_bytes":str(size),"seed":str(1048576+size+it),"expected_sha256":hashes[n],"actual_sha256":hashes[n],"payload_write_bytes":str(size),"payload_read_bytes":str(size),"total_bytes":str(size*2),"free_before":"499998720","free_after":"499990528","fflush_ok":"1","fsync_ok":"1","close_ok":"1","rename_rc":"0","rename_errno":"0","checksum_match":"1","retained":"1" if retained else "0","deleted":"0" if retained else "1","failure_op":"none","open_us":"1","write_us":"1","fflush_us":"1","fsync_us":"1","close_us":"1","rename_us":"1","read_verify_us":"1","delete_us":"1","status":"ok","error":"0"})
    for n in range(5):
        add("MOUNT_RESULT",{"kind":"remount","cycle":str(n),"mounted":"1","fs_type":"3","sector_bytes":"512","volume_sectors":"1048576","allocation_unit_bytes":"4096","cluster_count":"130811","total_bytes":"535801856","free_bytes":"499998720","read_bytes":"512","write_bytes":"0","elapsed_us":"1","status":"ok","error":"0"})
        add("PROBE_RESULT",{"cycle":str(n),"size_bytes":"65536","seed":"849697315","verified":"1","elapsed_us":"1","status":"ok","error":"0"})
        for ix in keep:
            size=65536 if ix<20 else 196608; it=ix if ix<20 else ix-20
            add("RETAINED_RESULT",{"cycle":str(n),"index":str(ix),"size_bytes":str(size),"seed":str(1048576+size+it),"expected_sha256":hashes[ix],"actual_sha256":hashes[ix],"checksum_match":"1","status":"ok","error":"0"})
    add("DIRECTORY_RESULT",{"entries":"11","owned_parts":"1","unknown_entries":"0","cleanup_count":"1","status":"ok","error":"0"})
    add("RENAME_RESULT",{"old_target_valid":"1","source_present":"1","rename_rc":"-1","rename_errno":"17","expected_errno":"17","old_target_preserved":"1","target_sha256":c._pattern_sha256(16384,0x610001),"source_sha256":c._pattern_sha256(24576,0x610002),"status":"ok","error":"0"})
    add("RECLAIM_RESULT",{"bytes_before":"499998720","bytes_after":"500002816","reclaimed_bytes":"4096","expected_bytes":"4096","residual_owned_files":"10","status":"ok","error":"0"})
    add("CLEANUP",{"sd_unmount_attempted":"1","sd_unmount_error":"0","host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1","power_off_error":"0"})
    add("COMPLETE",{"result":"io_complete","failure_stage":"none","error":"0","command_count":"4","read_bytes":"20000256","write_bytes":str(512+1055744+5353984),"mbr_write_count":"1","format_write_bytes":"1055744","io_write_bytes":"5353984","trim_requests":"1","erase_calls":"0","out_of_bounds_attempts":"0","io_file_count":"40","retained_file_bytes":"893440","probe_count":"5","mount_count":"6","remount_count":"5","retained_check_count":"30","reclaim_status":"ok","sd_unmount_attempted":"1","sd_unmount_error":"0","host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1","power_off_error":"0","bound":"1","scope":"bounded_fat32_filesystem_io","media_writes":"2000"})
    capture=b"".join(rows)
    parsed=c.parse_capture(capture,epoch,elf,i); check(parsed.result=="io_complete","complete synthetic success"); passes+=1
    for wire in (capture.replace(b"\n", b"\r\n"),
                 b"".join(line.replace(b"\n", b"\r\n") if n % 2 else line
                          for n, line in enumerate(rows))):
        result=c.parse_capture(wire,epoch,elf,i)
        check(result.result=="io_complete" and result.raw_bytes==len(wire),"CRLF/mixed wire framing or raw count"); passes+=1
    for malformed in (partial[:-1]+b"\r", partial[:-1]+b"\r\r\n",
                      partial.replace(b"reset_reason=1",b"reset_reason=1\rX"),
                      partial.replace(b"reset_reason=1",b"reset_reason=1\t"),
                      partial.replace(b"reset_reason=1",b"reset_reason=1\x7f"),
                      partial.replace(b"reset_reason=1",b"reset_reason=1\x0b")):
        must_fail(lambda malformed=malformed:c._parse_record(malformed),"invalid wire control accepted"); passes+=1
    with_limit=capture.splitlines(keepends=True)[-1]
    old_limit=c.MAX_RECORD
    try:
        c.MAX_RECORD=len(with_limit)+1
        c._parse_record(with_limit[:-1]+b"\r\n"); passes+=1
        c.MAX_RECORD=len(with_limit)
        must_fail(lambda:c._parse_record(with_limit[:-1]+b"\r\n"),"normalized record bypassed original wire cap"); passes+=1
    finally:
        c.MAX_RECORD=old_limit
    full_cleanup_failure=edit_event(capture,"CLEANUP",b"sd_unmount_error=0",b"sd_unmount_error=-7")
    for old,new in ((b"result=io_complete",b"result=failed"),
                    (b"failure_stage=none",b"failure_stage=cleanup"),
                    (b"error=0,command_count=4",b"error=-7,command_count=4"),
                    (b"sd_unmount_error=0",b"sd_unmount_error=-7")):
        full_cleanup_failure=edit_event(full_cleanup_failure,"COMPLETE",old,new)
    check(c.parse_capture(full_cleanup_failure,epoch,elf,i).result=="failed",
          "valid full-run cleanup failure rejected"); passes+=1
    mutations=(
        ("COMPLETE",b"media_writes=2000",b"media_writes=1"),
        ("COMPLETE",b"media_writes=2000",b"media_writes=13000"),
        ("COMPLETE",b"trim_requests=1",b"trim_requests=0"),
        ("COMPLETE",b"retained_check_count=30",b"retained_check_count=29"),
        ("COMPLETE",b"retained_file_bytes=893440",b"retained_file_bytes=786432"),
        ("MOUNT_RESULT",b"fs_type=3",b"fs_type=2"),
        ("MOUNT_RESULT",b"fs_type=3",b"fs_type=0"),
        ("MOUNT_RESULT",b"mounted=1",b"mounted=0"),
        ("MOUNT_RESULT",b"cycle=0",b"cycle=1"),
        ("DIRECTORY_RESULT",b"entries=11",b"entries=10"),
        ("RECLAIM_RESULT",b"bytes_after=500002816",b"bytes_after=500002817"),
        ("LAYOUT_RESULT",b"physical_lba=0",b"physical_lba=1"),
        ("FORMAT_RESULT",b"elapsed_us=1",b"elapsed_us=120000001"),
        ("FORMAT_RESULT",b"write_calls=263",b"write_calls=1"),
        ("FORMAT_RESULT",b"sector_bytes=512",b"sector_bytes=1024"),
        ("FORMAT_RESULT",b"read_bytes=4096",b"read_bytes=4097"),
        ("IO_RESULT",b"payload_write_bytes=65536",b"payload_write_bytes=1"),
        ("IO_RESULT",b"rename_rc=0",b"rename_rc=2"),
        ("HOST",b"error=0",b"error=2147483648"),
        ("BOOT",b"reset_reason=1",b"reset_reason=4294967296"),
        ("BOOT",b"reset_reason=1",b"reset_reason=9"),
        ("CLEANUP",b"host_deinit_error=0",b"host_deinit_error=-1"),
    )
    for event,old,new in mutations:
        altered=edit_event(capture,event,old,new)
        must_fail(lambda altered=altered:c.parse_capture(altered,epoch,elf,i),"negative record fixture accepted")
        passes+=1
    bad_intent=copy.deepcopy(i); bad_intent["private_cid_sha256"]="0"*64
    must_fail(lambda:c.parse_capture(capture,epoch,elf,bad_intent),"capture CID did not bind expected intent"); passes+=1
    must_fail(lambda:c.parse_capture(b"Guru Meditation Error\n"+capture,epoch,elf,i),"panic marker accepted in success capture"); passes+=1
    # A real early failure can be represented without fabricating successful setup records.
    failed=(record("BOOT",{"reset_reason":"1"})+record("TRANSPORT",tr)+record("COMPLETE",{
        "result":"failed","failure_stage":"resources","error":"-1","command_count":"0",
        "read_bytes":"0","write_bytes":"0","mbr_write_count":"0","format_write_bytes":"0",
        "io_write_bytes":"0","trim_requests":"0","erase_calls":"0","out_of_bounds_attempts":"0",
        "io_file_count":"0","retained_file_bytes":"0","probe_count":"0","mount_count":"0",
        "remount_count":"0","retained_check_count":"0","reclaim_status":"failed",
        "sd_unmount_attempted":"0","sd_unmount_error":"0","host_deinit_attempted":"0",
        "host_deinit_error":"0","power_off_attempted":"0","power_off_error":"0","bound":"0",
        "scope":"bounded_fat32_filesystem_io","media_writes":"0"}))
    check(c.parse_capture(failed,epoch,elf).result=="failed","valid early failure rejected"); passes+=1
    badfailed=failed.replace(b"error=-1",b"error=0")
    must_fail(lambda:c.parse_capture(badfailed,epoch,elf),"zero-error failed terminal accepted"); passes+=1
    hostless=(record("BOOT",{"reset_reason":"1"})+record("TRANSPORT",tr)+record("COMPLETE",{
        "result":"failed","failure_stage":"host","error":"-1","command_count":"0",
        "read_bytes":"0","write_bytes":"0","mbr_write_count":"0","format_write_bytes":"0",
        "io_write_bytes":"0","trim_requests":"0","erase_calls":"0","out_of_bounds_attempts":"0",
        "io_file_count":"0","retained_file_bytes":"0","probe_count":"0","mount_count":"0",
        "remount_count":"0","retained_check_count":"0","reclaim_status":"failed",
        "sd_unmount_attempted":"0","sd_unmount_error":"0","host_deinit_attempted":"0",
        "host_deinit_error":"0","power_off_attempted":"0","power_off_error":"0","bound":"0",
        "scope":"bounded_fat32_filesystem_io","media_writes":"0"}))
    must_fail(lambda:c.parse_capture(hostless,epoch,elf),"host failure without HOST error accepted"); passes+=1
    host_no_cleanup=(record("BOOT",{"reset_reason":"1"})+record("TRANSPORT",tr)
        +record("HOST",{"error":"-7"})+record("COMPLETE",{
        "result":"failed","failure_stage":"host","error":"-7","command_count":"0",
        "read_bytes":"0","write_bytes":"0","mbr_write_count":"0","format_write_bytes":"0",
        "io_write_bytes":"0","trim_requests":"0","erase_calls":"0","out_of_bounds_attempts":"0",
        "io_file_count":"0","retained_file_bytes":"0","probe_count":"0","mount_count":"0",
        "remount_count":"0","retained_check_count":"0","reclaim_status":"failed",
        "sd_unmount_attempted":"0","sd_unmount_error":"0","host_deinit_attempted":"0",
        "host_deinit_error":"0","power_off_attempted":"0","power_off_error":"0","bound":"0",
        "scope":"bounded_fat32_filesystem_io","media_writes":"0"}))
    check(c.parse_capture(host_no_cleanup,epoch,elf).result=="incomplete","missing cleanup was presented as complete failure proof"); passes+=1
    early_resource=(record("BOOT",{"reset_reason":"1"})+record("TRANSPORT",tr)+record("COMPLETE",{
        "result":"failed","failure_stage":"resources","error":"-2","command_count":"0",
        "read_bytes":"0","write_bytes":"0","mbr_write_count":"0","format_write_bytes":"0",
        "io_write_bytes":"0","trim_requests":"0","erase_calls":"0","out_of_bounds_attempts":"0",
        "io_file_count":"0","retained_file_bytes":"0","probe_count":"0","mount_count":"0",
        "remount_count":"0","retained_check_count":"0","reclaim_status":"failed",
        "sd_unmount_attempted":"0","sd_unmount_error":"0","host_deinit_attempted":"0",
        "host_deinit_error":"0","power_off_attempted":"0","power_off_error":"0","bound":"0",
        "scope":"bounded_fat32_filesystem_io","media_writes":"0"}))
    check(c.parse_capture(early_resource,epoch,elf).result=="failed","valid resource failure rejected"); passes+=1
    # A failed remount on cycle one is valid after a complete cycle-zero group.
    prefix=[]
    for line in capture.splitlines(keepends=True):
        if line.startswith(b"H38,1,MOUNT_RESULT,") and b"kind=remount" in line and b"cycle=1" in line:
            line=line.replace(b"mounted=1",b"mounted=0").replace(b"status=ok",b"status=failed").replace(b"error=0",b"error=-5")
            prefix.append(line)
            break
        prefix.append(line)
    # cycle one first unmounted cycle zero, then its attempted remount failed
    # with mounted=0. Final cleanup must not claim an unmount of a live mount.
    prefix.append(record("CLEANUP",{"sd_unmount_attempted":"0","sd_unmount_error":"0","host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1","power_off_error":"0"}))
    failed_complete={"result":"failed","failure_stage":"remount","error":"-5","command_count":"3",
        "read_bytes":"10000000","write_bytes":"6410240","mbr_write_count":"1","format_write_bytes":"1055744",
        "io_write_bytes":"5353984","trim_requests":"1","erase_calls":"0","out_of_bounds_attempts":"0",
        "io_file_count":"40","retained_file_bytes":"893440","probe_count":"1","mount_count":"3",
        "remount_count":"2","retained_check_count":"6","reclaim_status":"failed","sd_unmount_attempted":"1",
        "sd_unmount_error":"0","host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1",
        "power_off_error":"0","bound":"1","scope":"bounded_fat32_filesystem_io","media_writes":"2000"}
    early_cleanup_prefix=list(rows[:12])
    early_cleanup_prefix.append(record("CLEANUP",{"sd_unmount_attempted":"0","sd_unmount_error":"0",
        "host_deinit_attempted":"1","host_deinit_error":"-7","power_off_attempted":"1","power_off_error":"0"}))
    early_cleanup_terminal=dict(failed_complete,failure_stage="cleanup",error="-7",command_count="2",
        sd_unmount_attempted="0",sd_unmount_error="0",host_deinit_attempted="1",host_deinit_error="-7",
        power_off_attempted="1",power_off_error="0")
    early_cleanup_prefix.append(record("COMPLETE",early_cleanup_terminal))
    must_fail(lambda:c.parse_capture(b"".join(early_cleanup_prefix),epoch,elf),
              "cleanup failure accepted before full I/O and FINISH"); passes+=1
    failed_remount_terminal=dict(failed_complete,sd_unmount_attempted="0")
    prefix.append(record("COMPLETE",failed_remount_terminal))
    check(c.parse_capture(b"".join(prefix),epoch,elf).result=="failed","valid cycle-one failure prefix rejected"); passes+=1
    prior_io_failure=edit_event(b"".join(prefix),"IO_RESULT",b"failure_op=none",b"failure_op=write")
    prior_io_failure=edit_event(prior_io_failure,"IO_RESULT",b"status=ok,error=0",b"status=failed,error=-8")
    must_fail(lambda:c.parse_capture(prior_io_failure,epoch,elf),
              "remount failure concealed an earlier failed I/O record"); passes+=1
    prior_semantics_failure=edit_event(b"".join(prefix),"RETAINED_RESULT",b"status=ok,error=0",b"status=failed,error=-8")
    must_fail(lambda:c.parse_capture(prior_semantics_failure,epoch,elf),
              "remount failure concealed an earlier semantics failure"); passes+=1
    multiple_mount_failures=edit_event(b"".join(prefix),"MOUNT_RESULT",b"status=ok,error=0",b"status=failed,error=-8",occurrence=1)
    must_fail(lambda:c.parse_capture(multiple_mount_failures,epoch,elf),
              "terminal accepted multiple failed mount witnesses"); passes+=1
    # A matching failed FORMAT_RESULT still cannot prove complete failure
    # cleanup when acquired host/power resources are omitted from both records.
    format_prefix=list(rows[:12])
    format_prefix[-1]=format_prefix[-1].replace(b"status=ok,error=0",b"status=failed,error=-5")
    format_prefix.append(record("CLEANUP",{"sd_unmount_attempted":"0","sd_unmount_error":"0",
        "host_deinit_attempted":"0","host_deinit_error":"0","power_off_attempted":"0","power_off_error":"0"}))
    format_terminal=dict(failed_complete,failure_stage="format",error="-5",sd_unmount_attempted="0",
        host_deinit_attempted="0",power_off_attempted="0")
    format_prefix.append(record("COMPLETE",format_terminal))
    check(c.parse_capture(b"".join(format_prefix),epoch,elf).result=="incomplete",
          "format failure omitted acquired host/power cleanup accepted as complete"); passes+=1
    valid_format_prefix=list(format_prefix)
    valid_format_prefix[-2]=record("CLEANUP",{"sd_unmount_attempted":"0","sd_unmount_error":"0",
        "host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1","power_off_error":"0"})
    valid_format_terminal=dict(format_terminal,host_deinit_attempted="1",power_off_attempted="1")
    valid_format_prefix[-1]=record("COMPLETE",valid_format_terminal)
    valid_format_failure=b"".join(valid_format_prefix)
    check(c.parse_capture(valid_format_failure,epoch,elf,i).result=="failed",
          "valid format failure with expected intent rejected"); passes+=1
    wrong_expected_intent=copy.deepcopy(i); wrong_expected_intent["private_cid_sha256"]="0"*64
    must_fail(lambda:c.parse_capture(valid_format_failure,epoch,elf,wrong_expected_intent),
              "failed capture ignored a successful identity binding to expected intent"); passes+=1
    prior_host_error=edit_event(valid_format_failure,"HOST",b"error=0",b"error=-7")
    must_fail(lambda:c.parse_capture(prior_host_error,epoch,elf),
              "format failure concealed an earlier HOST error"); passes+=1
    bad_transport=edit_event(valid_format_failure,"TRANSPORT",b"backend=sdmmc",b"backend=other")
    must_fail(lambda:c.parse_capture(bad_transport,epoch,elf),
              "failed capture accepted a non-profile transport"); passes+=1
    ref=str(c.REFERENCE_SDHC32_SECTORS).encode()
    bad_geometry=edit_event(valid_format_failure,"GEOMETRY",b"sectors="+ref,b"sectors="+str(c.REFERENCE_SDHC32_SECTORS-1).encode())
    must_fail(lambda:c.parse_capture(bad_geometry,epoch,elf,i),
              "failed capture accepted mismatched preceding geometry"); passes+=1
    bad_reference=edit_event(valid_format_failure,"IDENTITY_MATCH",b"reference_epoch="+c.H35_REFERENCE_EPOCH.encode(),b"reference_epoch="+b"0"*32)
    must_fail(lambda:c.parse_capture(bad_reference,epoch,elf),
              "failed capture accepted a mismatched H35 reference epoch"); passes+=1
    # A truthful failed BIND may report a CID mismatch against expected intent.
    bind_prefix=list(rows[:9])
    bind_prefix[6]=bind_prefix[6].replace(b"serial=1",b"serial=2")
    bind_prefix[8]=record("IDENTITY_MATCH",{"reference_epoch":c.H35_REFERENCE_EPOCH,"match":"0","error":"-7"})
    bind_prefix.append(record("CLEANUP",{"sd_unmount_attempted":"0","sd_unmount_error":"0",
        "host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1","power_off_error":"0"}))
    bind_terminal=dict(failed_complete,failure_stage="bind",error="-7",sd_unmount_attempted="0")
    bind_terminal["host_deinit_attempted"]="1"; bind_terminal["power_off_attempted"]="1"
    bind_prefix.append(record("COMPLETE",bind_terminal))
    check(c.parse_capture(b"".join(bind_prefix),epoch,elf,i).result=="failed",
          "truthful failed BIND CID mismatch was compared as a successful binding"); passes+=1
    # The failed-remount witness reports whether the adapter is still mounted.
    # mounted=1 means cleanup must attempt an unmount; mounted=0 does not.
    live_mount=list(prefix)
    live_mount[-3]=live_mount[-3].replace(b"mounted=0",b"mounted=1")
    live_mount[-2]=record("CLEANUP",{"sd_unmount_attempted":"0","sd_unmount_error":"0",
        "host_deinit_attempted":"1","host_deinit_error":"0","power_off_attempted":"1","power_off_error":"0"})
    live_mount[-1]=record("COMPLETE",failed_remount_terminal)
    check(c.parse_capture(b"".join(live_mount),epoch,elf).result=="incomplete",
          "failed remount with live mount and omitted unmount accepted as complete"); passes+=1
    # Likewise, omitting every cleanup attempt after a failed remount is only
    # incomplete evidence even when the terminal exactly mirrors CLEANUP.
    remount_cleanup_omitted=b"".join(prefix)
    for old,new in ((b"host_deinit_attempted=1",b"host_deinit_attempted=0"),
                    (b"power_off_attempted=1",b"power_off_attempted=0")):
        remount_cleanup_omitted=edit_event(remount_cleanup_omitted,"CLEANUP",old,new)
        remount_cleanup_omitted=edit_event(remount_cleanup_omitted,"COMPLETE",old,new)
    check(c.parse_capture(remount_cleanup_omitted,epoch,elf).result=="incomplete",
          "failed remount with all acquired resources omitted from cleanup accepted"); passes+=1
    # Returned layout/format/mount errors carry closed, parseable failure witnesses.
    for stage, length, event, changes in (
        ("layout", 10, "LAYOUT_RESULT", ((b"readback_base64_private="+base64.b64encode(m), b"readback_base64_private=unavailable"),
                                          (b"readback_match=1", b"readback_match=0"))),
        ("format", 12, "FORMAT_RESULT", ((b"bpb_base64_private="+base64.b64encode(bpb), b"bpb_base64_private=unavailable"),
                                          (b"bpb_valid=1", b"bpb_valid=0"))),
        ("mount", 13, "MOUNT_RESULT", ((b"fs_type=3", b"fs_type=0"),
                                       (b"mounted=1", b"mounted=0"))),
    ):
        fixture=list(rows[:length])
        witness=fixture[-1]
        for old,new in changes:
            witness=witness.replace(old,new,1)
        witness=witness.replace(b"status=ok,error=0",b"status=failed,error=-5")
        fixture[-1]=witness
        if stage == "mount":
            fixture[-2]=fixture[-2].replace(b"bpb_valid=1",b"bpb_valid=0").replace(
                b"bpb_base64_private="+base64.b64encode(bpb),
                b"bpb_base64_private=unavailable")
        terminal=dict(failed_complete, failure_stage=stage, error="-5", sd_unmount_attempted="0")
        cleanup_row=rows[-2].replace(b"sd_unmount_attempted=1",b"sd_unmount_attempted=0")
        fixture.extend((cleanup_row, record("COMPLETE",terminal)))
        check(c.parse_capture(b"".join(fixture),epoch,elf).result=="failed",
              "returned failure witness rejected: "+stage)
        passes+=1
    remount_failure=b"".join(prefix)
    for stage in (b"reset", b"geometry", b"budget", b"timeout", b"semantics", b"mount", b"probe"):
        altered=remount_failure.replace(b"failure_stage=remount", b"failure_stage="+stage)
        try:
            result=c.parse_capture(altered,epoch,elf).result
        except c.ContractError:
            result="incomplete"
        check(result=="incomplete", "mismatched failure stage accepted")
        passes+=1
    missing_cleanup=remount_failure.replace(prefix[-2],b"")
    must_fail(lambda:c.parse_capture(missing_cleanup,epoch,elf),
              "missing cleanup with claimed attempts accepted"); passes+=1
    altered=remount_failure.replace(b"failure_stage=remount,error=-5",b"failure_stage=remount,error=-6")
    must_fail(lambda:c.parse_capture(altered,epoch,elf),"failure error mismatch accepted"); passes+=1
    print(f"H38 contract checks passed: {passes}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(run())
    except Exception as exc:
        print(f"H38 contract checks failed: {type(exc).__name__}")
        raise
