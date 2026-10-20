"""Regenerate LAB-03/v1 bytes; checked-in manifest is the comparison authority."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent / 'fixtures'

def generate():
    ROOT.mkdir(exist_ok=True)
    # Integer square-wave PCM avoids platform libm differences. Quiet 220Hz cue.
    pcm = b''.join(struct.pack('<h', 2500 if (i * 440 // 16000) % 2 else -2500)
                   for i in range(48000))
    (ROOT / 'tone.pcm').write_bytes(pcm)
    for name, count in [('short', 12), ('dense', 4096)]:
        records = []
        for i in range(count):
            due = i * 2800 // count
            records.append(struct.pack('<I4BH', due, (i * 7) % 120, (i * 11) % 120,
                                       (i * 7 + 5) % 120, (i * 11 + 7) % 120,
                                       [0xf800, 0x07e0, 0x001f][i % 3]))
        (ROOT / f'{name}.sketch').write_bytes(b'L3SK' + struct.pack('<HH', 1, count) + b''.join(records))
    assets = {p.name: {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
              for p in sorted(ROOT.iterdir()) if p.suffix in {'.pcm', '.sketch'}}
    messages = {}
    for mid, sketch in [('A', 'short.sketch'), ('B', 'dense.sketch'), ('C', None),
                        ('D', 'short.sketch'), ('E', 'short.sketch'), ('F', 'dense.sketch')]:
        messages[mid] = {'audio': 'tone.pcm', 'sketch': sketch, 'duration_ms': 3000,
                         'audio_delay_ms': 120 if mid == 'E' else 20,
                         'sketch_delay_ms': 120 if mid == 'F' else 20,
                         'chunk_bytes': 1024, 'chunk_delay_ms': 40 if mid in {'E', 'F'} else 0,
                         'status': 503 if mid == 'D' else 200}
    manifest = {'fixture_version': 'lab03-media/v1', 'server_version': 'LAB-03/v1',
                'raster': [120, 120], 'max_segments': 4096, 'sample_rate': 16000,
                'assets': assets, 'messages': messages}
    manifest_text = json.dumps(manifest, indent=2, sort_keys=True) + '\n'
    (ROOT / 'manifest.json').write_bytes(manifest_text.encode('utf-8'))

if __name__ == '__main__':
    generate()
