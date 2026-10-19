#!/usr/bin/env python3
"""Record an externally aimed BOX-3 camera + mic in a single synchronized file."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
parser=argparse.ArgumentParser()
parser.add_argument('--device',required=True,help='AVFoundation camera:microphone indices aimed at BOX-3')
parser.add_argument('--seconds',type=int,default=780)
parser.add_argument('--run-dir',required=True,type=Path)
args=parser.parse_args()
if not args.run_dir.is_dir(): parser.error('run directory must already exist')
output=args.run_dir/'synchronized-screen-audio.mkv'
if output.exists(): parser.error('refusing to overwrite recording')
started=time.monotonic_ns()
result=subprocess.run(['ffmpeg','-nostdin','-hide_banner','-f','avfoundation','-framerate','30',
                       '-i',args.device,'-t',str(args.seconds),'-c:v','libx264','-preset','ultrafast',
                       '-c:a','pcm_s16le',str(output)],capture_output=True,text=True)
metadata={'run_id':args.run_dir.name,'started_host_ns':started,'ended_host_ns':time.monotonic_ns(),
          'status':'CAPTURED_REQUIRES_CUE_REVIEW' if result.returncode==0 else 'UNAVAILABLE',
          'sha256':hashlib.sha256(output.read_bytes()).hexdigest() if output.exists() else None,
          'synchronization_cue_offset_seconds':None,'target_view_verified':False}
(args.run_dir/'synchronized-capture.json').write_text(json.dumps(metadata,indent=2)+'\n')
raise SystemExit(result.returncode)
