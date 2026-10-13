import sys, threading, time
from pathlib import Path
from unittest.mock import Mock
sys.path.insert(0, '/Users/lynnfrank/src/family-link/scripts')
from lab02_scenarios import Lab02ScenarioCoordinator, classify_scenario

class FakeServer:
    run_id='run'
    def post_action(self, action_id, payload, cancelled=None):
        return {'ok':True,'run_id':'run','action_id':action_id,'inbox_revision':2,'removed_message_id':'viewed','nearest_remaining_message_id':'neighbor'}

def ingest_case(runid, include_fetch):
    sink=[]; c=Lab02ScenarioCoordinator(FakeServer(),capture_active=lambda:True,event_sink=sink.append)
    c.ingest_line('LAB_TRACE t_us=10 event=snap_start gen=0 focus=4 msg=viewed snap_id=7 run_id=run')
    _, r = c.action('S-rm-focus','remove_message',{},viewed_message_id='viewed')
    if include_fetch:
        c.ingest_line(f'LAB_TRACE t_us=15 event=page_fetch gen=0 focus=4 msg=- op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id={runid}')
    c.ingest_line(f'LAB_TRACE t_us=20 event=page_apply gen=0 focus=5 msg=neighbor op_id=9 snap_id=7 revision=2 page_receipt_id=pg run_id={runid}')
    effects=[e for e in sink if e.get('kind')=='scenario_effect']
    verdict=classify_scenario('S-rm-focus',effects,action_receipt=r,expected_message_id='viewed')
    print('ingest run=%s fetch=%s effects=%d verdict=%s effect=%s' % (runid,include_fetch,len(effects),verdict,effects[0] if effects else None))
    c.close()
ingest_case('other',True)
ingest_case('run',False)
ingest_case('run',True)

server=Mock();server.run_id='run'; got=[]
def post(action_id,payload,cancelled=None):
    got.append(dict(payload)); return {'ok':True,'run_id':'run','action_id':action_id,'inbox_revision':1}
server.post_action.side_effect=post
c=Lab02ScenarioCoordinator(server,capture_active=lambda:True)
data={'x':1}; c._action_lock.acquire(); future=c.dispatch_action('S-fix-8','delay_ms',data); time.sleep(.05); data['x']=2; c._action_lock.release(); future.result(1); print('dispatch mutable caller data observed by server:',got[0].get('x')); c.close()
