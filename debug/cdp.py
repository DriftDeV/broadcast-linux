import json,sys,urllib.request,websocket
tabs=json.load(urllib.request.urlopen('http://127.0.0.1:9222/json/list'))
page=[t for t in tabs if t['type']=='page'][0]
ws=websocket.create_connection(page['webSocketDebuggerUrl'],timeout=20)
_id=[0]
def call(method,params=None):
    _id[0]+=1; ws.send(json.dumps({'id':_id[0],'method':method,'params':params or {}}))
    while True:
        r=json.loads(ws.recv())
        if r.get('id')==_id[0]: return r
def js(expr):
    r=call('Runtime.evaluate',{'expression':expr,'returnByValue':True,'awaitPromise':True})
    return r.get('result',{}).get('result',{}).get('value', r)
if __name__=='__main__':
    print(js(sys.argv[1]))
