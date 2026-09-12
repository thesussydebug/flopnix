import argparse,shutil,struct,time
from desktoptests import Guest,ROOT
from fscp import do_copy,entries
from newappstests import wait_for,openapp,appclick
from polishtests import move_window,icon
from transfertests import open_folder,row,arows,usb_bytes
from archivelimitgui import idle,close_checked
from usb import Fat16,make

def drag(g,start):
    w=g.window('Archive Manager');target=(w['x']+w['w']-15,w['y']+180)
    g.move(*start);g.button(True);g.move(start[0]+12,start[1]+2)
    wait_for(lambda:g.memory(g.symbols['dnd_on'],1)==b'\1')
    g.move(*target);g.button(False);idle(g)
    wait_for(lambda:g.memory(g.symbols['dnd_on'],1)==b'\0')

def run(memory):
    folder=ROOT/'out/archive-drops'/f'{memory}mb';folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img';usb=folder/'usb.img';seed=folder/'seed'
    shutil.copyfile(ROOT/'built/flopnix.img',img);make(str(usb),10)
    usb_source=b'From a USB folder.\0\xff';seed.write_bytes(usb_source)
    vol=Fat16(str(usb));vol.mkdir('DOCS');vol.put_into('DOCS',str(seed),'USB.TXT');vol.save()
    sources={'desktop/desk.txt':b'From the desktop.\n','in/deep/one.txt':b'Nested folder one.\0',
        'in/deep/two.txt':b'Nested folder two.\xff'}
    for name,body in {**sources,'autoexec':b'apptest\n'}.items():
        seed.write_bytes(body);do_copy(str(img),str(seed),name)
    do_copy(str(img),str(ROOT/'kexts/newappstest.kx'),'sys/apptest.kx')
    raw=bytearray(img.read_bytes());struct.pack_into('<IBBBB',raw,256*512,0x47464346,1,0,2,0);img.write_bytes(raw)
    g=Guest(folder,img,'pentium2',memory,usb=usb)
    try:
        g.wait('APPTEST READY',90);wait_for(lambda:g.window('Terminal'))
        w=g.window('Terminal');g.move(w['x']+60,w['y']+10);g.button(True);g.move(380,410);g.button(False)
        openapp(g,'Archive Manager');move_window(g,'Archive Manager',160)
        drag(g,icon(g,'desk.txt'));g.shot('desktop-drop')
        open_folder(g,'in/deep');move_window(g,'Files',0);g.key('ctrl+a')
        drag(g,row(g,arows(g,'in/deep').index('one.txt')));g.shot('multiple-folder-files')
        drag(g,row(g,arows(g,'in/deep').index('one.txt')));g.shot('duplicates-refused')
        appclick(g,'Files',112,80);idle(g);time.sleep(.5)
        g.click(*row(g,0));g.key('ret');idle(g);time.sleep(.5)
        drag(g,row(g,1));g.shot('usb-folder-drop');close_checked(g,'Files')
        w=g.window('Archive Manager');g.click(w['x']+60,w['y']+10)
        appclick(g,'Archive Manager',350,44);idle(g)
        def saved():
            b=img.read_bytes()
            for e in entries(b):
                if e['used'] and e['name']=='desktop/files.fpa':return b[e['start']*512:e['start']*512+e['size']]
        packed=wait_for(saved,80);assert packed[:5]==b'FPA1\x04',packed[:8]
        g.shot('saved');g.command('apptest finish');g.wait('APPTEST DONE: 0 failures')
    except Exception:
        g.shot('failure');print(g.serial()[-1800:],flush=True);raise
    finally:g.close()
    raw=img.read_bytes();table={e['name']:e for e in entries(raw) if e['used']}
    def content(name):
        e=table[name];return raw[e['start']*512:e['start']*512+e['size']]
    for name,body in sources.items():assert content(name)==body
    expected={name.split('/')[-1]:body for name,body in sources.items()};expected['usb.txt']=usb_source
    packed=content('desktop/files.fpa');offset=8;actual={}
    for _ in range(packed[4]):
        name=packed[offset:offset+24].split(b'\0')[0].decode().lower();size=struct.unpack_from('<I',packed,offset+24)[0]
        payload=packed[offset+28:offset+28+size]
        assert payload[:4]==b'PZ1\0' and payload[10:12]==b'\1\0'
        assert struct.unpack_from('<I',payload,4)[0]==size-12
        actual[name]=payload[12:];offset+=28+size
    assert offset==len(packed) and actual==expected
    assert usb_bytes(usb,'USB.TXT','DOCS')==usb_source
    print(f'PASS {memory} MB: desktop, nested folders, multiple files, duplicates, USB folder; saved archive bytes and originals verified',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--memory',type=int,default=8);run(p.parse_args().memory)
