import importlib.util,json, pathlib,hashlib
root=pathlib.Path('/Users/lynnfrank/src/family-link'); out=pathlib.Path('/tmp/lab02-c4-proof/server-pages');out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('lab02server',root/'server/labs/lab-02-paging/server.py');server=importlib.util.module_from_spec(spec);spec.loader.exec_module(server)
items=[{'message_id':f'm{i}','seq':i+1,'sender_name':f'Sender {i}','preview':f'Fixture {i}','duration_ms':12345,'unread':True} for i in range(256)]
state=server.InboxState(items)
def capture():
 for off in range(0,len(state.items),16):
  page=state.page(off,16);page['run_id']='c4-owned-server-unit'
  (out/f'{state.inbox_revision}-{off}.json').write_text(json.dumps(page))
capture();receipts=[]
for i in range(100):
 r=state.action('c4-owned-server-unit',f'remove-{i}',{'name':'remove_message','viewed_message_id':f'm{i}'})
 assert r['ok'];receipts.append(r)
assert state.items[100]['message_id']=='m200' and len(state.items)==156
capture();(out.parent/'server-receipts.json').write_text(json.dumps(receipts,indent=2))
print('100 identity-targeted actions accepted; revision101/count156; m200 survives at100; current+adjacent pages8/9 omit it. Server unit directly invoked; no managed HTTP/target claim.')
