#!/usr/bin/env python3
"""Versioned LAB-12 synthetic profile fixture service. Never connects to accounts."""
from __future__ import annotations
import argparse, hashlib, json, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT=Path(__file__).resolve().parent
VERSION="lab12-profile/v1"
SERVER_VERSION="LAB-12/v1"
LOCK=threading.Lock()
PROFILES={}
OPS={}
COUNTS={}
ALLOWED=set()

def fixture_hash():
    h=hashlib.sha256()
    for p in sorted((ROOT/'fixtures').glob('*.json')):
        h.update(p.name.encode()+b"\0"+p.read_bytes()+b"\0")
    return h.hexdigest()

def reset_state():
    global PROFILES, OPS, COUNTS, ALLOWED
    data=json.loads((ROOT/'fixtures/profiles.json').read_text())
    PROFILES={k:dict(v) for k,v in data['profiles'].items()}; OPS={}; COUNTS={"side_effects":0}
    ALLOWED=set(json.loads((ROOT/'fixtures/allowed-colors.json').read_text())['allowed'])

class Handler(BaseHTTPRequestHandler):
    server_version="LAB12Fixture/1"
    def log_message(self, fmt, *args):
        # Fixed-route diagnostics only; request bodies and headers are never logged.
        print(json.dumps({"event":"http","route":self.path.split('?')[0],"status":args[1] if len(args)>1 else ""}),flush=True)
    def send(self,status,obj):
        b=json.dumps(obj,separators=(',',':')).encode(); self.send_response(status)
        self.send_header('Content-Type','application/json'); self.send_header('Content-Length',str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path=='/health':
            return self.send(200,{"run_id":self.server.run_id,"fixture_sha256":fixture_hash(),"fixture_version":VERSION,"server_version":SERVER_VERSION})
        if self.path.startswith('/profile/'):
            endpoint=self.path.rsplit('/',1)[-1]
            with LOCK:
                if endpoint not in PROFILES: return self.send(404,{"error":"unknown_endpoint"})
                return self.send(200,{"schema":"fl.user-profile-fixture/1","user_id":"synthetic-user","endpoint":endpoint,**PROFILES[endpoint]})
        if self.path=='/fixtures':
            return self.send(200,{"reject_preview":json.loads((ROOT/'fixtures/reject-preview.json').read_text()),"offline_probe":json.loads((ROOT/'fixtures/offline-probe.json').read_text()),"layout":json.loads((ROOT/'fixtures/layout.json').read_text()),"message_color":json.loads((ROOT/'fixtures/message-color.json').read_text())})
        if self.path=='/stats':
            with LOCK:return self.send(200,{"side_effect_count":COUNTS['side_effects']})
        return self.send(404,{"error":"not_found"})
    def do_POST(self):
        if self.path=='/reset': reset_state(); return self.send(200,{"ok":True})
        return self.send(404,{"error":"not_found"})
    def do_PATCH(self):
        endpoint=self.path.rsplit('/',1)[-1]
        try:
            if int(self.headers.get('Content-Length','0'))>2048: return self.send(413,{"error":"invalid"})
            req=json.loads(self.rfile.read(int(self.headers.get('Content-Length','0'))))
        except Exception: return self.send(400,{"error":"invalid"})
        op=req.get('operation_id'); changes=req.get('changes'); revision=req.get('expected_revision')
        delay=req.get('fixture_delay_ms',0)
        if isinstance(delay,int) and 0<=delay<=1000: time.sleep(delay/1000)
        with LOCK:
            if endpoint not in PROFILES: return self.send(404,{"error":"unknown_endpoint"})
            if not isinstance(op,str) or not op or not isinstance(changes,dict): return self.send(400,{"error":"invalid"})
            fingerprint=json.dumps(req,sort_keys=True,separators=(',',':'))
            if op in OPS:
                old_fingerprint,result=OPS[op]
                if old_fingerprint!=fingerprint: return self.send(409,{"error":"identity_conflict"})
                return self.send(200,result)
            if req.get('fixture_reject') is True: return self.send(403,{"error":"rejected"})
            if any(k not in {'avatar_id','profile_accent'} for k in changes): return self.send(400,{"error":"invalid"})
            accent=changes.get('profile_accent')
            if accent is not None and (accent not in ALLOWED or len(accent)!=7): return self.send(422,{"error":"invalid"})
            current=PROFILES[endpoint]
            if revision != current['revision']: return self.send(409,{"error":"revision_conflict","revision":current['revision']})
            COUNTS["side_effects"]+=1; updated={**current,**changes,'revision':current['revision']+1}
            # Both synthetic endpoint sessions observe one canonical user profile.
            for key in PROFILES: PROFILES[key]=dict(updated)
            result={"schema":"fl.user-profile-fixture/1","user_id":"synthetic-user","endpoint":endpoint,**updated}; OPS[op]=(fingerprint,result)
            if req.get('fixture_drop_response') is True:
                self.close_connection=True
                self.connection.close()
                return
            return self.send(200,result)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--bind',default='127.0.0.1'); p.add_argument('--port',type=int,default=8771); p.add_argument('--run-id',required=True); a=p.parse_args()
    reset_state(); srv=ThreadingHTTPServer((a.bind,a.port),Handler); srv.run_id=a.run_id
    print(json.dumps({"event":"ready","port":a.port,"run_id":a.run_id,"fixture_sha256":fixture_hash()}),flush=True)
    try: srv.serve_forever(poll_interval=.1)
    finally: srv.server_close()
if __name__=='__main__': main()
