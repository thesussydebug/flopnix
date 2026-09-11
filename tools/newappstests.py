'Exercise apps with disposable images and local test servers.'
import argparse, pathlib, shutil, socketserver, struct, threading, time
from desktoptests import Guest, ROOT
from fscp import do_copy, entries, TABLE, ENTSZ

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address=True
    daemon_threads=True
class HTTP(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        request=b''
        while b'\r\n\r\n' not in request:
            chunk=self.request.recv(2048)
            if not chunk:return
            request+=chunk
        path=request.split(b' ')[1]
        if path==b'/large':body=b'x'*18000; size=len(body)
        elif path==b'/short':body=b'too short';size=500
        elif path==b'/next':body=b'<p>Second page &amp; a successful relative link.</p>';size=len(body)
        else:body=b'<html><h1>Local test page</h1><p>Hello &amp; welcome.</p><script>HIDDEN_SCRIPT</script><a href="/next">Next page</a></html>';size=len(body)
        try:
            self.request.sendall(f'HTTP/1.0 200 OK\r\nContent-Length: {size}\r\nContent-Type: text/html\r\n\r\n'.encode())
            for i in range(0,len(body),997):self.request.sendall(body[i:i+997]);time.sleep(.01)
        except (ConnectionResetError,ConnectionAbortedError,BrokenPipeError):pass
class Gopher(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10);request=b''
        while not request.endswith(b'\r\n'):
            b=self.request.recv(256)
            if not b:return
            request+=b
        if request==b'/doc\r\n':body=b'Hello from Gopher.\r\n..A dotted line.\r\n.\r\n'
        elif request.startswith(b'/search\t'):body=b'0Search result\t/doc\t10.0.2.2\t17070\r\n.\r\n'
        else:body=b'iLocal Gopher menu\tfake\tinvalid\t0\r\n0Read a document\t/doc\t10.0.2.2\t17070\r\n7Search\t/search\t10.0.2.2\t17070\r\n.\r\n'
        body=body.replace(b'17070',str(self.server.server_address[1]).encode())
        for i in range(0,len(body),17):self.request.sendall(body[i:i+17]);time.sleep(.01)

def wait_for(fn, seconds=35):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:
            result=fn()
            if result:return result
        except (AssertionError,FileNotFoundError):pass
        time.sleep(.2)
    raise AssertionError('Timed out waiting for app state')

def files(g):
    table=g.memory(g.symbols['table'],128*40);result=[]
    for i in range(128):
        e=table[i*40:(i+1)*40]
        if e[36]:result.append((e[:24].split(b'\0')[0].decode(),e[37],struct.unpack_from('<I',e,24)[0]))
    return result

def clipboard(g):return g.memory(g.symbols['c_buf'],g.value('c_len')).rstrip(b'\0')
def appclick(g,title,x,y):
    w=g.window(title);g.click(w['x']+3+x,w['y']+22+y)
def openapp(g,title):
    g.command('apptest open '+title);return wait_for(lambda:g.window(title))
def closeapp(g,title):
    g.command('apptest focus '+title)
    w=g.window(title);g.click(w['x']+w['w']-11,w['y']+10)

def resize(g,title):
    w=g.window(title)
    if w['x']+w['w']+40>=g.value('SW'):
        g.move(w['x']+60,w['y']+10);g.button(True);g.move(80,w['y']+10);g.button(False)
    for delta in (40,-40,40):
        w=g.window(title);x=w['x']+w['w']-5;y=w['y']+w['h']-5
        g.move(x,y);g.button(True);g.move(x+delta,y);g.button(False)
        assert g.window(title)['w']==w['w']+delta,'Resize did not take effect'
    return g.window(title)

def pick(g,path,dirs=False):
    assert g.value('ov_mouse'),'Picker absent'
    px=(g.value('SW')-300)//2;py=(g.value('SH')-214)//2
    root=[]
    for name,attr,_ in files(g):
        if '/' in name:
            folder=name.split('/')[0]
            if folder not in root:root.append(folder)
        elif attr&16:
            if name not in root:root.append(name)
        elif not dirs:root.append(name)
    parts=path.split('/') if path else []
    if parts:
        index=root.index(parts[0]);assert index<9,(path,root)
        g.click(px+60,py+22+index*16+8)
        if len(parts)>1:
            rows=['..']+[n for n,a,_ in files(g) if n.startswith(parts[0]+'/') and not a&16]
            index=rows.index(path);assert index<9
            g.click(px+60,py+22+index*16+8)
    g.click(px+40,py+214-16)
    wait_for(lambda:not g.value('ov_mouse'))
    time.sleep(1)

def run(a):
    folder=ROOT/'out/newapps-tests'/f'{a.memory}mb{"-flat" if a.no_gdi else ""}'
    folder.mkdir(parents=True,exist_ok=True);img=folder/'run.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    do_copy(str(img),str(ROOT/'kexts/newappstest.kx'),'sys/apptest.kx')
    do_copy(str(img),str(ROOT/'out/import.bad'),'desktop/import.kx')
    (folder/'autoexec').write_text('apptest open Character Map\n')
    do_copy(str(img),str(folder/'autoexec'),'autoexec')
    fixtures={'input/alpha.txt':b'Alpha document.\n'*40,'input/empty.txt':b'','input/bytes.bin':bytes(range(256))*4,'desktop/start.txt':b'Desktop folder.\n'}
    legacy=b'Legacy pack format.\n';checksum=0
    for byte in legacy:checksum=(((checksum<<1)|(checksum>>15))+byte)&65535
    fixtures['desktop/legacy.pz']=struct.pack('<4sIHBB',b'PZ1\0',len(legacy),checksum,1,0)+legacy
    fixtures['desktop/damaged.pz']=struct.pack('<4sIHBB',b'PZ1\0',len(legacy),checksum^1,1,0)+legacy
    for name,body in fixtures.items():
        source=folder/name.split('/')[-1];source.write_bytes(body);do_copy(str(img),str(source),name)
    d=bytearray(img.read_bytes());struct.pack_into('<IBBBB',d,256*512,0x47464346,1,0,2,0)
    if a.no_gdi:
        for e in entries(d):
            if e['used'] and e['name']=='sys/gdi.kx':d[TABLE*512+e['i']*ENTSZ+36]=0
    img.write_bytes(d)
    servers=[]
    for handler in (Gopher,HTTP):
        s=Server(('127.0.0.1',0),handler);threading.Thread(target=s.serve_forever,daemon=True).start();servers.append(s)
    gp,hp=[s.server_address[1] for s in servers]
    netcmd=f'apptest net {hp} {gp}'
    g=Guest(folder,img,'pentium2',a.memory,'user,model=ne2k_pci,id=net0',capture=True)
    try:
        g.wait('APPTEST READY',80);wait_for(lambda:g.window('Terminal'),80);time.sleep(3)
        if a.net_only:
            g.command(netcmd);g.wait('APPTEST NET DONE',60);g.command('apptest finish');g.wait('APPTEST DONE: 0 failures');print('PASS isolated network checks',flush=True);return
        openapp(g,'Character Map');w=resize(g,'Character Map');cw=w['w']-6;ch=w['h']-25;sw=(cw-36)//16;sh=(ch-80)//16;gx=(cw-sw*16)//2
        for value in (0,65,127,128,219,255):
            appclick(g,'Character Map',gx+value%16*sw+sw//2,40+value//16*sh+sh//2)
            assert g.value('c_len')==1 and g.memory(g.symbols['c_buf'],1)==bytes([value]),value
        g.shot('character-map');print('PASS all glyph ranges copy exact bytes',flush=True)

        appclick(g,'Character Map',gx+11*sw+sw//2,40+13*sh+sh//2)
        openapp(g,'Editor');g.key('ctrl+v');g.key('ctrl+c');assert clipboard(g)==b'\xdb';g.shot('editor-glyph');closeapp(g,'Editor')
        openapp(g,'Notes');g.key('ctrl+v');g.key('ctrl+c');assert clipboard(g)==b'\xdb';g.shot('notes-glyph');closeapp(g,'Notes')
        closeapp(g,'Character Map');print('PASS glyph paste into Editor and Notes',flush=True)
        openapp(g,'Base Converter');w=resize(g,'Base Converter');g.text('4294967295');cw=w['w']-6
        for row,want in enumerate((b'FFFFFFFF',b'4294967295',b'11111111111111111111111111111111',b'37777777777')):
            appclick(g,'Base Converter',cw-40,46+row*38);assert clipboard(g)==want,(row,clipboard(g))
        g.shot('base-converter');appclick(g,'Base Converter',100,80);g.key('ctrl+a');g.text('4294967296');g.shot('base-overflow')
        closeapp(g,'Base Converter');print('PASS live base conversions and full 32-bit output',flush=True)
        openapp(g,'2048');resize(g,'2048')
        for key in ('left','up','right','down')*8:g.key(key)
        g.shot('2048');g.key('u');g.shot('2048-undo');closeapp(g,'2048');print('PASS 2048 controls',flush=True)
        openapp(g,'Archive Manager');w=resize(g,'Archive Manager');ch=w['h']-25
        for name in ('input/alpha.txt','input/empty.txt','input/bytes.bin'):
            appclick(g,'Archive Manager',205,44);pick(g,name)
        g.shot('archive-created');appclick(g,'Archive Manager',350,44)
        wait_for(lambda:any(n=='desktop/files.fpa' for n,_,_ in files(g)));time.sleep(1)
        appclick(g,'Archive Manager',205,ch-44);pick(g,'',True)
        wait_for(lambda:any(n=='bytes.bin' for n,_,_ in files(g)),50)
        g.shot('archive-extracted');appclick(g,'Archive Manager',205,ch-44);pick(g,'',True);g.shot('archive-no-overwrite')
        closeapp(g,'Archive Manager');g.command('apptest file desktop/files.fpa');wait_for(lambda:g.window('Archive Manager'));g.shot('archive-reopened');closeapp(g,'Archive Manager')
        g.command('apptest file desktop/legacy.pz');w=wait_for(lambda:g.window('Archive Manager'));time.sleep(1)
        appclick(g,'Archive Manager',205,w['h']-25-44);pick(g,'',True);wait_for(lambda:any(n=='legacy' for n,_,_ in files(g)));closeapp(g,'Archive Manager')
        g.command('apptest file desktop/damaged.pz');w=wait_for(lambda:g.window('Archive Manager'));time.sleep(1)
        appclick(g,'Archive Manager',205,w['h']-25-44);assert not g.value('ov_mouse'),'Damaged archive enabled extraction'
        g.shot('archive-damaged');closeapp(g,'Archive Manager')
        print('PASS archives, legacy PZ, damaged-data refusal, conflicts and opener',flush=True)
        g.command(netcmd);g.wait('APPTEST NET DONE',60)
        assert 'APPTEST FAIL' not in g.serial(),g.serial()
        openapp(g,'Text Web');w=resize(g,'Text Web')
        def visit(url,needle):
            g.key('ctrl+l');g.key('ctrl+a');g.text(url);g.key('ret');time.sleep(3)
            appclick(g,'Text Web',355,44)
            wait_for(lambda:needle in clipboard(g));return clipboard(g)
        text=visit(f'gopher://10.0.2.2:{gp}/1',b'Local Gopher menu');g.shot('gopher-menu')
        appclick(g,'Text Web',275,44);appclick(g,'Text Web',90,103);time.sleep(3);appclick(g,'Text Web',355,44)
        assert b'Hello from Gopher.' in clipboard(g) and b'.A dotted line.' in clipboard(g);g.shot('gopher-document')
        visit(f'gopher://10.0.2.2:{gp}/1',b'Local Gopher menu')
        appclick(g,'Text Web',275,44);appclick(g,'Text Web',90,119);g.text('two words');g.key('ret');time.sleep(3);appclick(g,'Text Web',355,44)
        assert b'Search result' in clipboard(g);g.shot('gopher-search')
        text=visit(f'http://10.0.2.2:{hp}/',b'Hello & welcome.');assert b'HIDDEN_SCRIPT' not in text;g.shot('http-page')
        appclick(g,'Text Web',275,44);g.shot('http-links');appclick(g,'Text Web',90,103);time.sleep(3);appclick(g,'Text Web',355,44)
        assert b'Second page &' in clipboard(g);g.shot('http-followed')
        appclick(g,'Text Web',45,44);time.sleep(3);appclick(g,'Text Web',355,44);assert b'Hello & welcome.' in clipboard(g)
        appclick(g,'Text Web',125,44);time.sleep(3);appclick(g,'Text Web',355,44);assert b'Second page &' in clipboard(g)
        appclick(g,'Text Web',205,44);appclick(g,'Text Web',45,44);time.sleep(3);appclick(g,'Text Web',355,44);assert b'Second page &' in clipboard(g)
        g.shot('http-history');closeapp(g,'Text Web');print('PASS Gopher, HTTP text, links and back/forward',flush=True)
        g.command('apptest imports');g.command('apptest finish');g.wait('APPTEST DONE: 0 failures');g.shot('finished')
    except Exception:
        g.shot('failure');print(g.serial()[-2500:],flush=True);print(g.windows(),g.modules(),flush=True)
        print('faults',g.value('fault_recoveries'),g.memory(g.symbols['fault_hist'],192).hex(),flush=True);raise
    finally:
        g.close()
        for s in servers:s.shutdown();s.server_close()
    d=img.read_bytes()
    for name in ('alpha.txt','empty.txt','bytes.bin'):
        e=next(e for e in entries(d) if e['used'] and e['name']==name)
        assert d[e['start']*512:e['start']*512+e['size']]==fixtures['input/'+name],name
    e=next(e for e in entries(d) if e['used'] and e['name']=='legacy')
    assert d[e['start']*512:e['start']*512+e['size']]==legacy
    assert not any(e['used'] and e['name']=='damaged' for e in entries(d))
    print('PASS extracted files match byte-for-byte',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--memory',type=int,default=32);p.add_argument('--no-gdi',action='store_true');p.add_argument('--net-only',action='store_true');run(p.parse_args())
