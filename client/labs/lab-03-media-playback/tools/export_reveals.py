#!/usr/bin/env python3
"""Expand lossless UI reveal batches into auditable per-stroke proxy observations."""
import argparse
import gzip
import json
from pathlib import Path
import struct

parser=argparse.ArgumentParser()
parser.add_argument('serial',type=Path)
parser.add_argument('--out',required=True,type=Path)
args=parser.parse_args()
lines=args.serial.read_text().splitlines()
ready=next((i for i,line in enumerate(lines) if line.startswith('LAB03_READY ')),None)
if ready is None: parser.error('fresh ready marker missing')
rows=[json.loads(line[12:]) for line in lines[ready+1:] if line.startswith('LAB03_TRACE ')]
fixtures=Path(__file__).resolve().parents[4]/'server/labs/lab-03-media/fixtures'
assets={name:(fixtures/(name+'.sketch')).read_bytes() for name in ('short','dense')}
focus=[r for r in rows if r['event']=='focus']
starts={r['operation_id']:r['t_us'] for r in rows if r['event']=='intent_play'}
previous={};count=0
with gzip.open(args.out,'xt',encoding='utf-8') as out:
    for row in rows:
        if row['event']!='reveal_batch': continue
        op=row['operation_id'];last=previous.get(op,starts.get(op,row['t_us']))
        returned=any(last<r['t_us']<row['t_us'] and r['message_id']!=row['message_id'] for r in focus)
        previous[op]=row['t_us']
        asset=assets['dense' if row['message_id'] in ('B','F') else 'short']
        total=struct.unpack_from('<H',asset,6)[0]
        if total!=row['aux'] or not 0<=row['rendered']<=row['value']<=total:
            raise ValueError('reveal batch does not match fixed fixture')
        for index in range(row['rendered'],row['value']):
            due=struct.unpack_from('<I',asset,8+10*index)[0]
            item={'operation_id':op,'message_id':row['message_id'],'stroke_index':index,
                  'ui_invalidation_t_us':row['t_us'],'submitted_pcm_position_ms':row['position_ms'],
                  'due_ms':due,'proxy_lateness_ms':row['position_ms']-due,
                  'context':'focus_return' if returned else 'continuous_focus'}
            out.write(json.dumps(item,separators=(',',':'))+'\n');count+=1
print(json.dumps({'stroke_observations':count,'clock':'submitted_pcm_proxy','display':'UI invalidation proxy'}))
