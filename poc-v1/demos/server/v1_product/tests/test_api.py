import hashlib
import asyncio
import time
import importlib
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, AsyncMock

from fastapi.testclient import TestClient
from demos.server._shared.user_mailbox import minimal_wav

CID = '7f8a9a70-26dc-493b-aed6-410e976ebbe2'
HEADERS = {'Authorization':'Bearer test-token', 'X-User-Id':'a'}
RECIPIENT = dict(HEADERS, **{'X-User-Id':'b'})


class ProductApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        registry = root/'hangout.yaml'
        registry.write_text('''hangout: {id: test, name: Test}
users:
  - {id: a, name: A, pin: '1234', web_admin: true, web_password: fake-test-password}
  - {id: b, name: B, pin: '1234'}
  - {id: c, name: C, pin: '1234'}
endpoints:
  - {id: test-box, token: test-token, hangout_id: test}
''')
        self.env = patch.dict(os.environ, {'FAMILY_LINK_START_FRESH':'1', 'FAMILY_LINK_DATA_DIR':str(root/'data'),
                              'FAMILY_LINK_HANGOUT_REGISTRY':str(registry),
                              'FAMILY_LINK_MESSAGE_STORE':str(root/'store')})
        self.env.start()
        self.start()

    def start(self):
        name = 'demos.server.v1_product.server'
        self.server = importlib.reload(sys.modules[name]) if name in sys.modules else importlib.import_module(name)
        self.client = TestClient(self.server.app)
        self.client.__enter__()

    def restart(self):
        self.client.__exit__(None,None,None)
        self.start()

    def tearDown(self):
        self.client.__exit__(None,None,None)
        self.env.stop()
        self.tmp.cleanup()

    def create(self):
        return self.client.post('/v1/messages', headers=HEADERS, json={
            'client_message_id':CID, 'broadcast':True,
            'audio':{'codec':'pcm_s16le'}})

    def test_legacy_upload_stream_range_restart_state(self):
        wav = minimal_wav()
        response = self.client.post('/v1/messages',headers=HEADERS,
            data={'kind':'audio','to_user_id':'b'},files={'blob':('ignored.wav',wav)})
        self.assertEqual(response.status_code,200,response.text)
        seq = response.json()['messages'][0]['seq']
        path = f'/v1/messages/{seq}/blob'
        self.assertEqual(self.client.get(path,headers=RECIPIENT).content,wav)
        ranged = self.client.get(path,headers=dict(RECIPIENT,Range='bytes=44-63'))
        self.assertEqual(ranged.status_code,206)
        self.assertEqual(ranged.content,wav[44:64])
        self.assertEqual(self.client.get(path,headers=dict(RECIPIENT,Range='bytes=999999-')).status_code,416)
        self.client.put(f'/v1/messages/{seq}/read',headers=RECIPIENT,json={'position_ms':500})
        self.client.put('/v1/profile',headers=RECIPIENT,json={'avatar_slot':5})
        before = self.client.get('/v1/inbox',headers=RECIPIENT).json()
        self.restart()
        after = self.client.get('/v1/inbox',headers=RECIPIENT).json()
        self.assertEqual(before,after)
        self.assertEqual(self.client.get(path,headers=RECIPIENT).content,wav)

    def test_chunk_protocol_resume_complete_replay_and_authorization(self):
        create = self.create()
        self.assertEqual(create.status_code,201,create.text)
        mid = create.json()['message_id']
        pcm = b'\0\0'*32000
        sha = hashlib.sha256(pcm).hexdigest()
        chunkheaders = dict(HEADERS, **{'Content-Type':'application/octet-stream',
            'X-Chunk-SHA256':sha,'X-Chunk-Bytes':str(len(pcm)),
            'X-Chunk-Start-Ms':'0','X-Chunk-Duration-Ms':'2000'})
        url = f'/v1/messages/{mid}/audio/0'
        self.assertEqual(self.client.put(url,headers=chunkheaders,content=pcm).status_code,201)
        self.restart()
        self.assertEqual(self.create().status_code,200)
        self.assertEqual(self.client.get(f'/v1/messages/{mid}/upload',headers=HEADERS).json()['audio']['received'],[0])
        self.assertEqual(self.client.put(url,headers=chunkheaders,content=pcm).status_code,200)
        completion = {'audio_chunks':1,'duration_ms':2000,'source_audio_sha256':sha}
        done = self.client.post(f'/v1/messages/{mid}/complete',headers=HEADERS,json=completion)
        self.assertEqual(done.status_code,201,done.text)
        self.restart()
        self.assertEqual(self.client.post(f'/v1/messages/{mid}/complete',headers=HEADERS,json=completion).status_code,200)
        self.assertEqual(self.client.get(f'/v1/messages/{mid}/audio',headers=HEADERS).status_code,404)
        audio = self.client.get(f'/v1/messages/{mid}/audio',headers=RECIPIENT)
        self.assertEqual(audio.content[44:],pcm)
        self.assertEqual(audio.headers['etag'],'"'+hashlib.sha256(audio.content).hexdigest()+'"')
        self.assertEqual(self.client.get(f'/v1/messages/{mid}/upload',headers=RECIPIENT).status_code,404)
        self.assertEqual(len(self.client.get('/v1/inbox',headers=RECIPIENT).json()['messages']),2)

    def test_reject_invalid_media_metadata_and_oversized_body(self):
        bad = self.client.post('/v1/messages',headers=HEADERS,
            data={'kind':'audio','to_user_id':'b'},files={'blob':('fake.wav',b'not-wav')})
        self.assertEqual(bad.status_code,400)
        self.assertEqual(self.client.post('/v1/messages',headers=HEADERS,json={
            'to_user_id':'b','audio':{'codec':'opus'}}).status_code,400)
        self.assertEqual(self.client.post('/v1/messages',headers=dict(HEADERS,**{'Content-Length':'9000000'}),content=b'x').status_code,413)
        self.assertEqual(len(self.client.get('/v1/inbox',headers=RECIPIENT).json()['messages']),1)

    def test_multipart_retry_receipt_after_restart(self):
        def send():
            return self.client.post('/v1/messages',headers=HEADERS,
                data={'kind':'audio','to_user_id':'b','client_message_id':CID},
                files={'blob':('test.wav',minimal_wav())})
        first = send()
        self.assertEqual(first.status_code,200)
        self.restart()
        receipt = self.client.get(f'/v1/outgoing/{CID}',headers=HEADERS)
        self.assertEqual(receipt.status_code,200)
        self.assertEqual(receipt.json()['message_id'],first.json()['messages'][0]['message_id'])
        self.assertEqual(self.client.get(f'/v1/outgoing/{CID}',headers=RECIPIENT).status_code,404)
        self.assertEqual(send().json(),first.json())
        self.assertEqual(len(self.client.get('/v1/inbox',headers=RECIPIENT).json()['messages']),2)

    def test_inbox_paging_keeps_complete_history(self):
        for _ in range(10):
            response = self.client.post('/v1/messages',headers=HEADERS,
                data={'kind':'audio','to_user_id':'b'},files={'blob':('test.wav',minimal_wav(0.01))})
            self.assertEqual(response.status_code,200)
        latest = self.client.get('/v1/inbox',headers=RECIPIENT).json()
        self.assertEqual(len(latest['messages']),8)
        self.assertEqual(latest['total_messages'],11)
        self.assertTrue(latest['has_more'])
        older = self.client.get('/v1/inbox',headers=RECIPIENT,
                               params={'before_seq':latest['next_before_seq']}).json()
        self.assertEqual(len(older['messages']),3)
        self.assertFalse(older['has_more'])
        self.restart()
        self.assertEqual(self.client.get('/v1/inbox',headers=RECIPIENT).json(),latest)

    def test_websocket_uses_product_registry(self):
        with self.client.websocket_connect('/v1/ws') as websocket:
            websocket.send_json({'type':'hello','token':'test-token'})
            self.assertEqual(websocket.receive_json()['type'],'hello_ok')
            response = self.client.post('/v1/messages',headers=HEADERS,
                data={'kind':'audio','to_user_id':'b'},files={'blob':('test.wav',minimal_wav())})
            self.assertEqual(response.status_code,200)
            # This single test endpoint is signed into no user; notification dispatch
            # is separately covered through the server notification-failure test.

    def test_stalled_notification_has_bounded_response(self):
        async def stalled(*args):
            await asyncio.Event().wait()
        start = time.monotonic()
        with self.assertLogs('demos.server.v1_product.server',level='ERROR'), patch.object(self.server.v1_ws,'notify_inbox',new=stalled):
            response = self.client.post('/v1/messages',headers=HEADERS,
                data={'kind':'audio','broadcast':'true'},files={'blob':('test.wav',minimal_wav())})
        self.assertEqual(response.status_code,200)
        self.assertLess(time.monotonic()-start,3)
        self.assertEqual(len(response.json()['messages']),2)

    def test_admin_login_and_notification_failure_still_accepted(self):
        admin = self.client.post('/v1/admin/login',json={'username':'a','password':'fake-test-password'})
        self.assertEqual(admin.status_code,200,admin.text)
        with self.assertLogs('demos.server.v1_product.server', level='ERROR'), patch.object(self.server.v1_ws,'notify_inbox',new=AsyncMock(side_effect=RuntimeError('test notification failure'))):
            response = self.client.post('/v1/admin/messages',
                headers={'Authorization':'Bearer '+admin.json()['token']},
                data={'kind':'audio','to_user_id':'b'},files={'blob':('test.wav',minimal_wav())})
        self.assertEqual(response.status_code,200,response.text)
        self.restart()
        self.assertEqual(len(self.client.get('/v1/inbox',headers=RECIPIENT).json()['messages']),2)


if __name__ == '__main__':
    unittest.main()
