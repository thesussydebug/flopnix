'Check file transfers using disposable floppy and USB images.'
import argparse,hashlib,shutil,struct,time
from desktoptests import Guest,ROOT
from newappstests import wait_for,files,appclick
from polishtests import move_window,closeapp,icon
from fscp import do_copy,entries
from usb import Fat16, make

def row(g,index):
    w=g.window('Files');return w['x']+3+210,w['y']+22+91+index*16+8

def arows(g,directory):
    prefix=directory+'/' if directory else '';children={}
    for n,attr,_ in files(g):
        if not n.startswith(prefix):continue
        rest=n[len(prefix):]
        if not rest:continue
        child=rest.split('/')[0];children[child]=('/' in rest or bool(attr&16)) or children.get(child,False)
    return (['..'] if directory else [])+sorted(children,key=lambda n:(not children[n],n.lower()))

def select_a(g,directory,name):
    g.click(*row(g,arows(g,directory).index(name)))

def open_folder(g,path):
    g.command('apptest folder '+path if path else 'apptest open Files');wait_for(lambda:g.window('Files'));time.sleep(.5)

def close_files(g):
    move_window(g,'Files',0);closeapp(g,'Files')

def usb_entries(vol,parent=None):
    found=vol.entries()
    for part in (parent or '').split('/'):
        if not part:continue
        e=next((e for e in found if e['name'].lower()==part.lower() and e['attr']&16),None)
        if not e:return []
        found=[];c=e['clus'];raw=bytearray()
        for _ in range(vol.clusters+1):
            if not 2<=c<0xfff8:break
            off=vol._cluster_off(c);raw+=vol.data[off:off+vol.spc*512];c=vol._fat_get(c)
        for off in range(0,len(raw),32):
            d=raw[off:off+32]
            if d[0]==0:break
            if d[0]==229 or d[11]&8 or d[0]==46:continue
            name=d[:8].decode('latin1').rstrip();ext=d[8:11].decode('latin1').rstrip()
            found.append(dict(name=name+('.'+ext if ext else ''),attr=d[11],clus=struct.unpack_from('<H',d,26)[0],size=struct.unpack_from('<I',d,28)[0]))
    return found

def usb_bytes(path,name,parent=None):
    vol=Fat16(str(path));found=usb_entries(vol,parent)
    e=next((e for e in found if e['name'].lower()==name.lower()),None)
    if not e:return None
    c=e['clus'];left=e['size'];out=bytearray()
    for _ in range(vol.clusters+1):
        if not left:break
        if not 2<=c<0xfff8:return None
        n=min(left,vol.spc*512);off=vol._cluster_off(c);out+=vol.data[off:off+n];left-=n;c=vol._fat_get(c)
    return bytes(out) if not left else None

def file_state(g):
    owner=g.modules()['sys/files.kx']['id']
    base=struct.unpack('<I',g.memory(g.symbols['kext_priv_phys']+owner*4,4))[0]
    if base:
        size=struct.unpack('<I',g.memory(g.symbols['kext_priv_len']+owner*4,4))[0]
    else:
        base=g.value('arena_rw')
        size=struct.unpack_from('<I',g.memory(g.symbols['memory'],32),12)[0]-base
    return g.memory(base,size)

def large_copy(g,usb,want,memory):
    open_folder(g,'');select_a(g,'','large.fpa');g.key('ctrl+c');time.sleep(.6)
    appclick(g,'Files',112,80);time.sleep(.6);g.key('ctrl+v')
    start=time.monotonic();next_report=start+30;refused=False
    while usb_bytes(usb,'LARGE.FPA')!=want:
        now=time.monotonic()
        if g.value('run_win')==-1:
            state=file_state(g)
            if memory<=4 and b'Not enough free RAM to copy this file' in state:
                refused=True;break
            assert b'Destination write failed; source kept' not in state, 'USB write failed'
        assert g.value('hang_ask_win')==-1,'A progressing copy was called unresponsive'
        if now-start>300:raise AssertionError('Large archive copy did not finish')
        if now>=next_report:
            print('Copy still active after',int(now-start),'seconds',flush=True);next_report=now+30
        time.sleep(.3)
    wait_for(lambda:g.value('run_win')==-1,60)
    assert g.value('hang_ask_win')==-1 and not g.value('ov_mouse')
    assert any(n=='large.fpa' and size==len(want) for n,_,size in files(g))
    if refused:
        assert usb_bytes(usb,'LARGE.FPA') is None
        print('PASS large-file RAM shortage reported; source kept and UI responsive',flush=True)
    else:print('PASS 320 KiB copy in',round(time.monotonic()-start,1),'seconds; USB resets:',g.value('usb_dbg_resets'),flush=True)
    g.shot('large-archive');close_files(g)

def qol(g,usb):
    move_window(g,'Terminal',320)

    open_folder(g,'dup');select_a(g,'dup','d.txt');appclick(g,'Files',65,8);g.menu('Duplicate')
    wait_for(lambda:any(n=='dup/d_10.txt' for n,_,_ in files(g)));g.shot('duplicate-ten');close_files(g)
    d=(g.folder/'run.img').read_bytes();table={e['name']:e for e in entries(d) if e['used']}
    for name,want in (('dup/d_9.txt',b'Keep nine'),('dup/d_10.txt',b'Original')):
        e=table[name];assert d[e['start']*512:e['start']*512+e['size']]==want

    open_folder(g,'pool');select_a(g,'pool','a.txt');g.key('ctrl+x');time.sleep(.5);close_files(g)
    open_folder(g,'in');appclick(g,'Files',273,36)
    wait_for(lambda:any(n=='in/a.txt' for n,_,_ in files(g)) and not any(n=='pool/a.txt' for n,_,_ in files(g)))
    g.shot('toolbar-cut-paste');close_files(g)

    open_folder(g,'pool');appclick(g,'Files',16,8);g.menu('New Folder')
    wait_for(lambda:any(n=='pool/New folder' and attr&16 for n,attr,_ in files(g)));time.sleep(.7)
    g.text('e');g.key('ret');wait_for(lambda:any(n=='pool/e' and attr&16 for n,attr,_ in files(g)))
    select_a(g,'pool','e');g.key('ctrl+c');time.sleep(.5);appclick(g,'Files',112,80);time.sleep(.6);appclick(g,'Files',273,36)
    wait_for(lambda:any(e['name']=='E' and e['attr']&16 for e in Fat16(str(usb)).entries()),60)
    assert usb_entries(Fat16(str(usb)),'E')==[];g.shot('empty-folder-to-usb')
    time.sleep(.8);names=sorted(Fat16(str(usb)).entries(),key=lambda e:(not bool(e['attr']&16),e['name'].lower()))
    g.click(*row(g,next(i for i,e in enumerate(names) if e['name']=='E')));g.key('ctrl+c');time.sleep(.5);appclick(g,'Files',273,36)
    wait_for(lambda:any(e['name']=='E_1' and e['attr']&16 for e in Fat16(str(usb)).entries()),60)
    close_files(g)

    g.click(*icon(g,'box'),True);g.menu('Paste')
    wait_for(lambda:any(n=='desktop/box/e' and attr&16 for n,attr,_ in files(g)),60)
    g.shot('empty-folder-context-paste')
    g.command('apptest finish');g.wait('APPTEST DONE: 0 failures')
    print('PASS Cut toolbar, nested New Folder, empty USB folders, same-folder copy, desktop folder Paste',flush=True)

def run(a):
    folder=ROOT/'out/transfer-tests'/f'{a.memory}mb{"-qol" if a.qol_only else "-large" if a.large_only else ""}';folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img';usb=folder/'usb.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    source_usb=ROOT/'usb.img'
    original=hashlib.sha256(source_usb.read_bytes()).digest() if source_usb.exists() else None
    if original is not None: shutil.copyfile(source_usb,usb)
    else: make(str(usb),10)
    vol=Fat16(str(usb))
    for name in ('FXIN','FXOUT'):
        assert not vol.find(name),'Test fixture name is occupied: '+name
        vol.mkdir(name)
    seed=folder/'seed';seed.write_bytes(b'USB source\0\xff\n');vol.put_into('FXIN',str(seed),'USB0.FPA');vol.save()
    fixtures={'autoexec':b'apptest\n','desktop/out.fpa':b'Outgoing\0\xff',
              'in/back.fpa':b'Incoming\0\xfe','in/pack.fpa':b'Archive\0\xff'*300,
              'in/package-one.fpa':b'First long name','in/package-two.fpa':b'Second long name',
              'qtree/sub/n.txt':b'Nested tree', 'pool/a.txt':b'Alpha','pool/b.txt':b'Beta','cut/c.txt':b'Cut C','cut/d.txt':b'Cut D',
              'desktop/d.txt':b'Existing D','large.fpa':bytes(range(256))*1280}
    if a.qol_only:
        fixtures['desktop/box/x.txt']=b'Folder marker';fixtures['dup/d.txt']=b'Original'
        for i in range(1,10):fixtures[f'dup/d_{i}.txt']=b'Keep nine' if i==9 else b'Existing'
    for name,data in fixtures.items():seed.write_bytes(data);do_copy(str(img),str(seed),name)
    do_copy(str(img),str(ROOT/'kexts/newappstest.kx'),'sys/apptest.kx')
    data=bytearray(img.read_bytes());struct.pack_into('<IBBBB',data,256*512,0x47464346,1,0,2,0);img.write_bytes(data)
    g=Guest(folder,img,'pentium2',a.memory,usb=usb)
    try:
        g.wait('APPTEST READY',90);wait_for(lambda:g.window('Terminal'));time.sleep(2)
        if a.large_only:
            move_window(g,'Terminal',320)
            large_copy(g,usb,fixtures['large.fpa'],a.memory)
            g.command('apptest finish');g.wait('APPTEST DONE: 0 failures')
            assert original is None or hashlib.sha256(source_usb.read_bytes()).digest()==original
            return
        if a.qol_only:
            qol(g,usb)
            assert original is None or hashlib.sha256(source_usb.read_bytes()).digest()==original
            return
        move_window(g,'Terminal',320);open_folder(g,'in');move_window(g,'Files',180)

        f=g.window('Files');g.move(*icon(g,'out.fpa'));g.button(True);g.move(f['x']+220,f['y']+280);g.button(False)
        wait_for(lambda:any(n=='in/out.fpa' for n,_,_ in files(g)))
        g.move(*row(g,arows(g,'in').index('back.fpa')));g.button(True);g.move(110,420);g.button(False)
        wait_for(lambda:any(n=='desktop/back.fpa' for n,_,_ in files(g)))
        assert not any(n=='in/back.fpa' for n,_,_ in files(g));g.shot('drag-roundtrip')
        close_files(g)

        open_folder(g,'pool');g.key('ctrl+a');g.key('ctrl+c');time.sleep(.6);close_files(g)
        g.click(110,420,True);g.menu('Paste')
        wait_for(lambda:all(any(n=='desktop/'+f for n,_,_ in files(g)) for f in ('a.txt','b.txt')))
        assert all(any(n=='pool/'+f for n,_,_ in files(g)) for f in ('a.txt','b.txt'));g.shot('multi-paste')

        open_folder(g,'cut');g.key('ctrl+a');g.key('ctrl+x');time.sleep(.6);close_files(g)
        g.click(110,420);g.key('ctrl+v')
        wait_for(lambda:any(n=='desktop/c.txt' for n,_,_ in files(g)))
        assert not any(n=='cut/c.txt' for n,_,_ in files(g)) and any(n=='cut/d.txt' for n,_,_ in files(g))
        wait_for(lambda:g.memory(g.symbols['c_buf'],g.value('c_len')).rstrip(b'\0')==b'a:cut/d.txt')
        g.shot('partial-cut-kept')

        g.click(*icon(g,'d.txt'),True);g.menu('Rename');g.text('old.txt');g.key('ret')
        wait_for(lambda:any(n=='desktop/old.txt' for n,_,_ in files(g)))
        g.click(110,420);g.key('ctrl+v');wait_for(lambda:not any(n=='cut/d.txt' for n,_,_ in files(g)))

        for source,want in (('pack.fpa','PACK.FPA'),('package-one.fpa','PACKAG~1.FPA'),('package-two.fpa','PACKAG~2.FPA')):
            open_folder(g,'in');select_a(g,'in',source);g.key('ctrl+c');time.sleep(.6)
            appclick(g,'Files',112,80);time.sleep(.7);g.key('ctrl+v')
            wait_for(lambda:usb_bytes(usb,want)==fixtures['in/'+source],90)
            assert any(n=='in/'+source for n,_,_ in files(g));g.shot('usb-'+want.replace('~','-'));close_files(g)

        open_folder(g,'in');appclick(g,'Files',112,80);time.sleep(.7)
        vol=Fat16(str(usb));rootnames=sorted(vol.entries(),key=lambda e:(not bool(e['attr']&16),e['name'].lower()))
        i=next(i for i,e in enumerate(rootnames) if e['name']=='FXIN');g.click(*row(g,i));g.key('ret');time.sleep(.5)
        g.key('down');g.key('down');g.key('ctrl+x');time.sleep(.6);g.key('backspace');time.sleep(.5)
        i=next(i for i,e in enumerate(rootnames) if e['name']=='FXOUT');g.click(*row(g,i));g.key('ret');time.sleep(.5);g.key('ctrl+v')
        wait_for(lambda:usb_bytes(usb,'USB0.FPA','FXOUT')==b'USB source\0\xff\n',90)
        wait_for(lambda:usb_bytes(usb,'USB0.FPA','FXIN') is None,60);g.shot('usb-folder-move')
        g.click(*row(g,1));g.key('ctrl+c');time.sleep(.6);close_files(g)
        g.click(110,420);g.key('ctrl+v')
        wait_for(lambda:any(n.lower()=='desktop/usb0.fpa' for n,_,_ in files(g)))
        g.shot('usb-to-desktop')

        open_folder(g,'');select_a(g,'','qtree');g.key('ctrl+c');time.sleep(.6)
        appclick(g,'Files',112,80);time.sleep(.6);g.key('ctrl+v')
        wait_for(lambda:usb_bytes(usb,'N.TXT','QTREE/SUB')==b'Nested tree',90);g.shot('folder-to-usb');close_files(g)
        open_folder(g,'');appclick(g,'Files',112,80);time.sleep(.6)
        names=sorted(Fat16(str(usb)).entries(),key=lambda e:(not bool(e['attr']&16),e['name'].lower()))
        g.click(*row(g,next(i for i,e in enumerate(names) if e['name']=='FXOUT')));g.key('ctrl+c');time.sleep(.6);close_files(g)
        g.click(110,420);g.key('ctrl+v')
        wait_for(lambda:any(n.lower()=='desktop/fxout/usb0.fpa' for n,_,_ in files(g)),90);g.shot('usb-folder-to-desktop')

        large_copy(g,usb,fixtures['large.fpa'],a.memory)
        g.command('apptest finish');g.wait('APPTEST DONE: 0 failures')
        print('PASS drag roundtrip, multi-paste, cross-app cut/retry, USB archive names, USB file moves, nested folder copies and large-file handling',flush=True)
    except Exception:
        g.shot('failure');print(g.serial()[-2400:],flush=True);print(files(g),flush=True);raise
    finally:g.close()
    assert original is None or hashlib.sha256(source_usb.read_bytes()).digest()==original
    d=img.read_bytes();table={e['name']:e for e in entries(d) if e['used']}
    for dst,source in (('desktop/back.fpa','in/back.fpa'),('in/out.fpa','desktop/out.fpa'),('desktop/d.txt','cut/d.txt'),('desktop/old.txt','desktop/d.txt')):
        e=table[dst];assert d[e['start']*512:e['start']*512+e['size']]==fixtures[source]
    print('PASS final bytes preserved; original USB image unchanged',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--memory',type=int,default=32);p.add_argument('--qol-only',action='store_true');p.add_argument('--large-only',action='store_true');run(p.parse_args())
