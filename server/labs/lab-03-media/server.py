#!/usr/bin/env python3
"""Fixed LAB-03/v1 service. No mutable timing/failure controls during a run."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import time
from urllib.parse import urlsplit, parse_qs

ROOT = Path(__file__).resolve().parent / 'fixtures'
MANIFEST_BYTES = (ROOT / 'manifest.json').read_bytes()
MANIFEST = json.loads(MANIFEST_BYTES)
FIXTURE_HASH = hashlib.sha256(MANIFEST_BYTES).hexdigest()
ASSETS = {name: (ROOT / name).read_bytes() for name in MANIFEST['assets']}
for name, data in ASSETS.items():
    assert hashlib.sha256(data).hexdigest() == MANIFEST['assets'][name]['sha256']
    assert len(data) == MANIFEST['assets'][name]['bytes']

class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = False

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass  # Never log request URLs, addresses, or headers.

    def reply(self, status, data, content='application/json', delay=0, chunk=65536, gap=0):
        time.sleep(delay / 1000)
        self.send_response(status)
        self.send_header('Content-Type', content)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('X-Fixture-SHA256', FIXTURE_HASH)
        self.send_header('X-Run-ID', self.server.run_id)
        self.end_headers()
        for offset in range(0, len(data), chunk):
            self.wfile.write(data[offset:offset + chunk])
            self.wfile.flush()
            if gap:
                time.sleep(gap / 1000)

    def do_GET(self):
        parsed = urlsplit(self.path)
        op = self.headers.get('X-Operation-ID', '0')
        if not re.fullmatch(r'[0-9]{1,10}', op):
            op = 'invalid'
        start = time.monotonic_ns()
        status, mid, asset = 404, '', ''
        try:
            if parsed.path == '/health':
                status = 200
                self.reply(200, json.dumps({'run_id': self.server.run_id, 'fixture_version': MANIFEST['fixture_version'],
                                           'fixture_sha256': FIXTURE_HASH, 'server_version': MANIFEST['server_version']}).encode())
            elif parsed.path == '/inbox':
                page = parse_qs(parsed.query).get('page', ['0'])[0]
                ids = list('ABCDEF') if page == '0' else ['C', 'B', 'A']
                items = [{'message_id': m, 'sender_name': 'Fixture ' + m,
                          'duration_ms': 3000, 'preview': 'LAB-03 media'} for m in ids]
                status = 200
                self.reply(200, json.dumps({'schema': 'inbox-summary/v1', 'revision': page,
                                           'page_size': 16, 'items': items}).encode())
            else:
                match = re.fullmatch(r'/media/([A-F])/(audio|sketch)', parsed.path)
                if not match:
                    self.reply(404, b'{}')
                    return
                mid, asset = match.groups()
                msg = MANIFEST['messages'][mid]
                status = msg['status'] if msg[asset] else 404
                data = ASSETS[msg[asset]] if status == 200 else b'{}'
                # Slow audio E and sketch F have fixed delays only on that asset.
                slow = (mid == 'E' and asset == 'audio') or (mid == 'F' and asset == 'sketch')
                self.reply(status, data, 'application/octet-stream', msg[asset + '_delay_ms'],
                           msg['chunk_bytes'], msg['chunk_delay_ms'] if slow else 0)
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            status = 499
        finally:
            print(json.dumps({'source': 'server', 'kind': 'request', 'run_id': self.server.run_id,
                              'operation_id': op, 'message_id': mid, 'asset': asset, 'status': status,
                              'start_ns': start, 'end_ns': time.monotonic_ns()}), flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8766)
    parser.add_argument('--run-id', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}', args.run_id):
        parser.error('unsafe run ID')
    server = Server((args.bind, args.port), Handler)
    server.run_id = args.run_id
    server.serve_forever(poll_interval=0.1)

if __name__ == '__main__':
    main()
