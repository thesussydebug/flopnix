'Check compact apps and desktop folder operations in QEMU.'
import argparse,shutil,struct,time
from desktoptests import Guest,ROOT
from fscp import do_copy,entries,TABLE,ENTSZ
from newappstests import wait_for,openapp,closeapp,appclick,files

def closeapp(g,title):
    g.command('apptest focus '+title)
    w=g.window(title)
    g.click(w['x']+60,w['y']+10)
    g.click(w['x']+w['w']-11,w['y']+10)
    wait_for(lambda:not any(w['title']==title for w in g.windows()))

def move_window(g,title,x):
    w=g.window(title);g.move(w['x']+60,w['y']+10);g.button(True);g.move(x+60,w['y']+10);g.button(False)

def desktop_names(g):
    names=[]
    for n,_,_ in files(g):
        if n.startswith('desktop/'):
            child=n.split('/')[1]
            if child and child not in names:names.append(child)
    return names

def icon(g,name):
    i=desktop_names(g).index(name);rows=(g.value('SH')-28-26)//56
    return 8+(i//rows)*76+32,26+(i%rows)*56+16

def timers(g):
    owner=g.modules()['sys/breakout.kx']['id'];data=g.memory(g.symbols['timers'],16*20)
    return sum(struct.unpack_from('<I',data,i*20+8)[0]!=0 and struct.unpack_from('<i',data,i*20+16)[0]==owner for i in range(16))

def run(a):
    folder=ROOT/'out/polish-tests'/f'{a.memory}mb{"-flat" if a.no_gdi else ""}';folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    do_copy(str(img),str(ROOT/'kexts/newappstest.kx'),'sys/apptest.kx')
    fixtures={'autoexec':b'apptest\n','desktop/docs/inside.txt':b'Nested file.\n','desktop/loose.txt':b'Drag this file.\n',
              'desktop/pack.fpa':b'FPA1\0\0\0\0','extras/note.txt':b'Folder drag.\n'}
    for n,data in fixtures.items():
        p=folder/'seed';p.write_bytes(data);do_copy(str(img),str(p),n)
    data=bytearray(img.read_bytes());struct.pack_into('<IBBBB',data,256*512,0x47464346,1,0,2,0)
    if a.no_gdi:
        for e in entries(data):
            if e['used'] and e['name']=='sys/gdi.kx':data[TABLE*512+e['i']*ENTSZ+36]=0
    img.write_bytes(data);g=Guest(folder,img,'pentium2',a.memory)
    try:
        g.wait('APPTEST READY',80);wait_for(lambda:g.window('Terminal'),80);time.sleep(2)
        if not a.folders_only:
            w=openapp(g,'2048');assert w['w']==278 and w['h']==357
            for k in ('left','up','right','down')*5:g.key(k)
            g.shot('2048');g.key('u');time.sleep(.6);closeapp(g,'2048');print('PASS compact 2048',flush=True)
            w=openapp(g,'System Info');assert w['w']==444 and w['h']==239;g.shot('system-info');closeapp(g,'System Info')
            w=openapp(g,'Settings');assert w['h']==345
            for x,height,name in ((210,359,'mouse'),(367,387,'network'),(220,397,'ipv4'),(355,407,'adapter')):
                appclick(g,'Settings',x,20 if name in ('mouse','network') else 68)
                wait_for(lambda:g.window('Settings')['h']==height);g.shot('settings-'+name)
            appclick(g,'Settings',55,20);wait_for(lambda:g.window('Settings')['h']==345)
            appclick(g,'Settings',100,212);wait_for(lambda:g.window('Settings')['h']==385);g.shot('wallpaper')
            appclick(g,'Settings',330,40);wait_for(lambda:g.window('Settings')['h']==337);g.shot('wallpaper-bitmap')
            appclick(g,'Settings',300,293);wait_for(lambda:g.window('Settings')['h']==345);closeapp(g,'Settings')
            print('PASS Settings fits all pages and returns',flush=True)
            openapp(g,'Breakout');assert timers(g)==0;g.shot('breakout-ready');g.key('spc');time.sleep(.25);assert timers(g)==1
            g.key('right');g.shot('breakout-running');g.key('spc');time.sleep(.2);assert timers(g)==0
            g.key('spc');g.command('apptest focus Terminal');time.sleep(.25);assert timers(g)==0
            closeapp(g,'Breakout');assert timers(g)==0;openapp(g,'Breakout');assert timers(g)==0;closeapp(g,'Breakout')
            print('PASS Breakout play, pause, focus and timer cleanup',flush=True)
        g.command('apptest folders');assert 'APPTEST FAIL' not in g.serial(),g.serial()
        move_window(g,'Terminal',320);g.shot('desktop-folders')
        assert desktop_names(g)==['docs','loose.txt','pack.fpa']

        g.move(*icon(g,'loose.txt'));g.button(True);g.move(*icon(g,'docs'));time.sleep(.3);g.button(False)
        wait_for(lambda:any(n=='desktop/docs/loose.txt' for n,_,_ in files(g)))
        assert not any(n=='desktop/loose.txt' for n,_,_ in files(g));g.shot('desktop-file-moved')

        g.click(*icon(g,'docs'),True);g.menu('Open');wait_for(lambda:g.window('Files'));g.shot('desktop-folder-open')
        g.key('down');g.key('down');g.key('ctrl+c')
        wait_for(lambda:b'a:desktop/docs/inside.txt' in g.memory(g.symbols['c_buf'],g.value('c_len')))
        move_window(g,'Files',0);closeapp(g,'Files');move_window(g,'Terminal',320)
        g.click(110,420,True);g.menu('New folder');g.text('work');g.key('ret')
        wait_for(lambda:any(n=='desktop/work' and attr&16 for n,attr,_ in files(g)));g.shot('desktop-new-folder')

        g.command('apptest folder extras');wait_for(lambda:g.window('Files'));g.key('backspace');time.sleep(.3);move_window(g,'Files',180)
        f=g.window('Files');g.move(f['x']+3+195,f['y']+22+91+16+8);g.button(True);g.move(110,380);time.sleep(.3);g.button(False)
        wait_for(lambda:any(n=='desktop/extras/note.txt' for n,_,_ in files(g)))
        assert not any(n=='extras/note.txt' for n,_,_ in files(g));g.shot('desktop-folder-moved')
        move_window(g,'Files',0);closeapp(g,'Files');move_window(g,'Terminal',320)

        g.click(*icon(g,'docs'),True);g.menu('Rename');g.text('papers');g.key('ret');time.sleep(.3)
        assert any(n=='desktop/docs/inside.txt' for n,_,_ in files(g))
        g.key('ctrl+a');g.text('data');g.key('ret')
        wait_for(lambda:any(n=='desktop/data/loose.txt' for n,_,_ in files(g)));g.shot('desktop-folder-renamed')
        g.click(*icon(g,'data'),True);g.menu('Copy');g.click(110,420,True);g.menu('Paste')
        wait_for(lambda:any(n=='desktop/C1/loose.txt' for n,_,_ in files(g)));g.shot('desktop-folder-copy')

        g.command('apptest folder desktop/data');wait_for(lambda:g.window('Files'))
        g.key('down');g.key('down');g.key('ctrl+c');time.sleep(.6)
        move_window(g,'Files',0);closeapp(g,'Files')
        g.command('apptest folder desktop/work');wait_for(lambda:g.window('Files'));g.key('ctrl+v')
        wait_for(lambda:any(n=='desktop/work/inside.txt' for n,_,_ in files(g)))
        assert any(n=='desktop/data/inside.txt' for n,_,_ in files(g))
        g.shot('files-folder-paste');move_window(g,'Files',0);closeapp(g,'Files')

        move_window(g,'Terminal',24);openapp(g,'Paint');g.key('ctrl+s')
        wait_for(lambda:g.value('ov_mouse'))
        px=(g.value('SW')-300)//2;py=(g.value('SH')-258)//2
        g.click(px+60,py+22+16+8)
        folders=['..']
        for n,attr,_ in files(g):
            if not n.startswith('desktop/'):continue
            rest=n[len('desktop/'):];child=rest.split('/')[0]
            if ('/' in rest or attr&16) and child not in folders:folders.append(child)
        g.click(px+60,py+22+folders.index('work')*16+8)
        for _ in range(8):g.key('backspace')
        g.text('a');g.shot('nested-save-picker');g.key('ret')
        wait_for(lambda:any(n=='desktop/work/a.bmp' for n,_,_ in files(g)))
        closeapp(g,'Paint');print('PASS Files copy and nested Paint save',flush=True)
        g.command('apptest finish');g.wait('APPTEST DONE: 0 failures');print('PASS desktop folders, real moves, rename and copy',flush=True)
    except Exception:
        g.shot('failure');print(g.serial()[-3000:],flush=True);print(g.windows(),flush=True);raise
    finally:g.close()
    d=img.read_bytes();lookup={e['name']:e for e in entries(d) if e['used']}
    for name,want in {'desktop/data/inside.txt':fixtures['desktop/docs/inside.txt'],'desktop/data/loose.txt':fixtures['desktop/loose.txt'],
                      'desktop/extras/note.txt':fixtures['extras/note.txt'],'desktop/C1/loose.txt':fixtures['desktop/loose.txt']}.items():
        e=lookup[name];assert d[e['start']*512:e['start']*512+e['size']]==want
    print('PASS final file bytes preserved',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--memory',type=int,default=32);p.add_argument('--no-gdi',action='store_true');p.add_argument('--folders-only',action='store_true');run(p.parse_args())
