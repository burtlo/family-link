"""Synthetic media only. Run: python -m unittest discover -s demos/server/v1_product/tests -v"""
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch

from demos.server._shared.hangout_registry import Hangout, HangoutRegistry, User
from demos.server._shared.user_mailbox import bootstrap_mailbox, minimal_wav
from demos.server.v1_product.archive import MessageArchive, ArchiveError

REGISTRY = HangoutRegistry(Hangout('test', 'Test'),
    {u: User(u, u, '1234') for u in ('a', 'b', 'c')}, {}, None)
CID = '7f8a9a70-26dc-493b-aed6-410e976ebbe2'


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / 'archive'
        self.archive = MessageArchive(self.root, free_floor=0)

    def tearDown(self):
        if self.archive is not None:
            self.archive.close()
        self.tmp.cleanup()

    def reopen(self):
        self.archive.close()
        self.archive = MessageArchive(self.root, free_floor=0)

    def create(self):
        req = dict(protocol='family-message/1', client_message_id=CID, to_user_id=None,
                   broadcast=True, audio=dict(codec='pcm_s16le', sample_rate_hz=16000, channels=1, target_chunk_ms=2000))
        return self.archive.create_upload('a', 'A', ['b', 'c'], req), req

    def put(self, mid, seq=0, data=b'\0\0'*16000, start=0):
        declared = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest(), start_ms=start, duration_ms=len(data)//32)
        return self.archive.put_chunk(mid, 'a', seq, io.BytesIO(data), declared)

    def complete(self, mid, data=b'\0\0'*16000):
        req = dict(audio_chunks=1, duration_ms=len(data)//32, source_audio_sha256=hashlib.sha256(data).hexdigest(), sketch_sequences=[], closed_reason='button')
        return self.archive.complete_upload(mid, 'a', req), req

    def test_accepted_broadcast_restart_and_state(self):
        box = bootstrap_mailbox(Path(self.tmp.name)/'data', REGISTRY, archive=self.archive)
        sent = box.post_audio('a', broadcast=True, blob=minimal_wav())
        before = [(m.to_user, m.seq, m.message_id) for m in sent]
        box.mark_read('b', sent[0].seq, 350)
        box.set_profile('b', avatar_slot=4)
        self.reopen()
        restored = bootstrap_mailbox(Path(self.tmp.name)/'data', REGISTRY, archive=self.archive)
        after = [(m.to_user, m.seq, m.message_id) for u in ('b','c') for m in restored.inboxes[u] if not m.system]
        self.assertEqual(before, after)
        self.assertEqual(restored.read_blob('b', sent[0].seq), minimal_wav())
        self.assertTrue(restored.inbox_payload('b')['messages'][-1]['read'])
        self.assertEqual(restored.profile_dict('b')['avatar_slot'], 4)
        self.assertEqual(len(self.archive.index), 2)  # one welcome, one shared message

    def test_chunks_resume_idempotent_complete_and_hash_conflict(self):
        (m, new), req = self.create()
        self.assertTrue(new)
        mid = m['message_id']
        self.put(mid)
        self.reopen()
        self.assertFalse(self.archive.create_upload('a', 'A', ['b','c'], req)[1])
        self.assertFalse(self.put(mid)[1])
        self.assertRaises(ArchiveError, self.put, mid, data=b'\x01\0'*16000)
        (accepted, fresh), completion = self.complete(mid)
        self.assertTrue(fresh)
        self.assertEqual([r['user_id'] for r in accepted['recipients']], ['b','c'])
        self.reopen()
        replay, fresh = self.archive.complete_upload(mid, 'a', completion)
        self.assertFalse(fresh)
        self.assertEqual(accepted, replay)
        self.assertEqual(len(self.archive.index), 1)

    def test_missing_chunk_and_bad_hash_preserve_upload(self):
        (m, _), _ = self.create()
        self.assertRaises(ArchiveError, self.complete, m['message_id'])
        self.put(m['message_id'])
        _, req = self.complete(m['message_id'])
        req['source_audio_sha256'] = '0'*64
        self.assertRaises(ArchiveError, self.archive.complete_upload, m['message_id'], 'a', req)

    def test_multipart_idempotence_and_conflict(self):
        first = self.archive.import_wav('a','A',['b'],io.BytesIO(minimal_wav()),client_id=CID)
        self.reopen()
        second = self.archive.import_wav('a','A',['b'],io.BytesIO(minimal_wav()),client_id=CID)
        self.assertEqual(first, second)
        self.assertRaises(ArchiveError, self.archive.import_wav, 'a','A',['c'],io.BytesIO(minimal_wav()),client_id=CID)

    def test_corrupt_archive_fails_closed(self):
        m = self.archive.import_wav('a','A',['b'],io.BytesIO(minimal_wav()))
        (self.archive.paths[m['message_id']]/'media.wav').write_bytes(b'corrupt')
        self.archive.close()
        self.archive = None
        self.assertRaises(RuntimeError, MessageArchive, self.root, free_floor=0)

    def test_sketch_shared_once_and_recovered(self):
        from demos.server.h27_sketch.codec import pack_sketch
        sketch = pack_sketch([(0,0,10,10),(200,2,12,12)])
        accepted = self.archive.import_wav('a','A',['b','c'],io.BytesIO(minimal_wav()),
                                           sketch=io.BytesIO(sketch),broadcast=True)
        self.reopen()
        manifest = self.archive.index[accepted['message_id']]
        self.assertEqual(manifest['sketch']['sha256'],hashlib.sha256(sketch).hexdigest())
        self.assertEqual((self.archive.paths[accepted['message_id']]/'sketch.flsk').read_bytes(),sketch)
        self.assertEqual(len(list((self.root/'messages').glob('*/*/*/sketch.flsk'))),1)

    def test_failed_user_state_write_rolls_back_visible_profile(self):
        box = bootstrap_mailbox(Path(self.tmp.name)/'data',REGISTRY,archive=self.archive)
        box.set_profile('b',avatar_slot=4)
        with patch.object(self.archive,'save_user_state',side_effect=OSError('disk failure')):
            self.assertRaises(OSError,box.set_profile,'b',avatar_slot=7)
        self.assertEqual(box.profile_dict('b')['avatar_slot'],4)

    def test_full_length_chunk_finalization_bounded_memory(self):
        (metadata,_),_ = self.create()
        mid = metadata['message_id']
        pcm = b'\0'*64000
        digest = hashlib.sha256()
        for seq in range(90):
            self.put(mid,seq=seq,data=pcm,start=seq*2000)
            digest.update(pcm)
        request = dict(audio_chunks=90,duration_ms=180000,source_audio_sha256=digest.hexdigest(),
                       sketch_sequences=[],closed_reason='duration_limit')
        tracemalloc.start()
        accepted,_ = self.archive.complete_upload(mid,'a',request)
        _,peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        self.assertEqual(accepted['audio']['bytes'],5760044)
        self.assertLess(peak,1024*1024)

    def test_disk_failure_before_commit_no_publication(self):
        with patch.object(self.archive, '_space', side_effect=ArchiveError(507,'full')):
            self.assertRaises(ArchiveError, self.archive.import_wav, 'a','A',['b'],io.BytesIO(minimal_wav()))
        self.assertEqual(self.archive.index, {})
        self.assertEqual(list((self.root/'incoming').iterdir()), [])

    def test_full_length_import_bounded_python_memory(self):
        import struct
        path = Path(self.tmp.name)/'long.wav'
        with path.open('wb') as out:
            size = 180*16000*2
            out.write(struct.pack('<4sI4s4sIHHIIHH4sI',b'RIFF',size+36,b'WAVE',b'fmt ',16,1,1,16000,32000,2,16,b'data',size))
            for _ in range(180):
                out.write(b'\0'*32000)
        tracemalloc.start()
        with path.open('rb') as source:
            result = self.archive.import_wav('a','A',['b'],source)
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        self.assertEqual(result['duration_ms'],180000)
        self.assertLess(peak,1024*1024)

    def test_second_server_cannot_open_same_root(self):
        self.assertRaises(OSError, MessageArchive, self.root, free_floor=0)

    def test_process_crash_commit_boundaries(self):
        self.archive.close()
        self.archive = None
        code = '''import io,sys
from pathlib import Path
from demos.server.v1_product.archive import MessageArchive
from demos.server._shared.user_mailbox import minimal_wav
store=MessageArchive(Path(sys.argv[1]),free_floor=0)
store.import_wav('a','A',['b','c'],io.BytesIO(minimal_wav()))
'''
        for boundary, expected in [('before_commit',0),('after_commit',1)]:
            root = Path(self.tmp.name)/boundary
            env = dict(os.environ, FAMILY_LINK_PRODUCT_CRASH_AT=boundary)
            run = subprocess.run([sys.executable,'-c',code,str(root)],env=env,capture_output=True)
            self.assertEqual(run.returncode,86,run.stderr.decode())
            with_archive = MessageArchive(root,free_floor=0)
            try:
                self.assertEqual(len(with_archive.index),expected)
                if expected:
                    self.assertEqual(len(next(iter(with_archive.index.values()))['recipients']),2)
            finally:
                with_archive.close()

    def test_process_crashes_resume_chunks_and_completion(self):
        self.archive.close()
        self.archive = None
        code = """import hashlib,io,sys
from pathlib import Path
from demos.server.v1_product.archive import MessageArchive
store=MessageArchive(Path(sys.argv[1]),free_floor=0)
req={'client_message_id':'7f8a9a70-26dc-493b-aed6-410e976ebbe2','broadcast':True}
m,_=store.create_upload('a','A',['b','c'],req)
data=b'\\0'*32000
sha=hashlib.sha256(data).hexdigest()
store.put_chunk(m['message_id'],'a',0,io.BytesIO(data),dict(bytes=32000,sha256=sha,start_ms=0,duration_ms=1000))
store.complete_upload(m['message_id'],'a',dict(audio_chunks=1,duration_ms=1000,source_audio_sha256=sha,sketch_sequences=[],closed_reason='button'))
"""
        for boundary in ('chunk_file','chunk_ack','assembled','before_commit','after_commit'):
            root = Path(self.tmp.name)/boundary
            run = subprocess.run([sys.executable,'-c',code,str(root)],
                env=dict(os.environ,FAMILY_LINK_PRODUCT_CRASH_AT=boundary),capture_output=True)
            self.assertEqual(run.returncode,86,run.stderr.decode())
            archive = MessageArchive(root,free_floor=0)
            try:
                m,_=archive.create_upload('a','A',['b','c'],{'client_message_id':CID,'broadcast':True})
                mid=m['message_id']
                data=b'\0'*32000
                sha=hashlib.sha256(data).hexdigest()
                if m['state']=='open':
                    archive.put_chunk(mid,'a',0,io.BytesIO(data),dict(bytes=32000,sha256=sha,start_ms=0,duration_ms=1000))
                result,_=archive.complete_upload(mid,'a',dict(audio_chunks=1,duration_ms=1000,source_audio_sha256=sha,sketch_sequences=[],closed_reason='button'))
                self.assertEqual(len(archive.index),1)
                self.assertEqual(len(result['recipients']),2)
            finally:
                archive.close()


if __name__ == '__main__':
    unittest.main()
