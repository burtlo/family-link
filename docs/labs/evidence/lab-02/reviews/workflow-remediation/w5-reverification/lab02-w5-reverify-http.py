import socket, sys, threading
from pathlib import Path
sys.path.insert(0, '/Users/lynnfrank/src/family-link/scripts')
from lab02_server import ManagedLab02Server
from lab02_workflow_config import Lab02WorkflowConfig
root=Path('/Users/lynnfrank/src/family-link')
s=socket.socket();s.bind(('127.0.0.1',0));port=s.getsockname()[1];s.close()
cfg=Lab02WorkflowConfig(root/'server/labs/lab-02-paging/server.py','127.0.0.1',port,64,'nominal',{'startup':2,'health':2,'seed':2,'readiness':2,'action':2,'cleanup':1},f'http://127.0.0.1:{port}')
m=ManagedLab02Server(cfg,run_id='verify-run');m.start()
try:
  p={'name':'push_item','item':{'preview':'once'},'run_id':m.run_id,'action_id':'duplicate'}
  a=m._request('/lab/script',payload=p);b=m._request('/lab/script',payload=p)
  print('duplicate real HTTP:',a['inbox_revision'],b['inbox_revision'],a['message_id']==b['message_id'],a['action_completed_ns']==b['action_completed_ns'])
  out=[]
  def post(i): out.append(m._request('/lab/script',payload={'name':'push_item','item':{'preview':str(i)},'run_id':m.run_id,'action_id':'parallel-'+str(i)}))
  ts=[threading.Thread(target=post,args=(i,)) for i in range(2)]
  [t.start() for t in ts];[t.join() for t in ts]
  print('concurrent real HTTP receipts:',sorted((x['action_id'],x['inbox_revision'],x['ok']) for x in out))
finally:m.close()
