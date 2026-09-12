import argparse,random,shutil,struct,time
from desktoptests import Guest,ROOT
from fscp import do_copy,entries
from newappstests import wait_for,files,appclick,openapp,pick

def idle(g):
    wait_for(lambda:g.memory(g.symbols['busy_on'],1)==b'\0',80)

def close_checked(g,title):
    idle(g)
    g.command('apptest close '+title)
    wait_for(lambda:not any(w['title']==title for w in g.windows()))

def run(memory):
    folder=ROOT/'out/archive-limits'/f'{memory}mb';folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    do_copy(str(img),str(ROOT/'kexts/newappstest.kx'),'sys/apptest.kx')
    size=65536 if memory==4 else 131072
    rng=random.Random(17);source=bytes(rng.randrange(32,127) for _ in range(size))
    fixture=folder/'seed';fixture.write_bytes(source);do_copy(str(img),str(fixture),'input/first.txt')
    editor_name='input/editor.txt' if memory==4 else 'input/first.txt'
    editor_source=source*2 if memory==4 else source
    if memory==4:fixture.write_bytes(editor_source);do_copy(str(img),str(fixture),editor_name)
    second=bytes(rng.randrange(32,127) for _ in range(65536))
    if memory>4:fixture.write_bytes(second);do_copy(str(img),str(fixture),'input/second.txt')
    fixture.write_bytes(b'apptest\n');do_copy(str(img),str(fixture),'autoexec')
    raw=bytearray(img.read_bytes());struct.pack_into('<IBBBB',raw,256*512,0x47464346,1,0,2,0);img.write_bytes(raw)
    g=Guest(folder,img,'pentium2',memory)
    try:
        g.wait('APPTEST READY',80);wait_for(lambda:g.window('Terminal'),80)
        print('Detected RAM KiB:',struct.unpack('<I',g.memory(0x700c,4))[0],flush=True)
        openapp(g,'Archive Manager')
        for name in ('input/first.txt',) if memory==4 else ('input/first.txt','input/second.txt'):
            appclick(g,'Archive Manager',205,44);pick(g,name)
            idle(g)
        appclick(g,'Archive Manager',350,44)
        wait_for(lambda:any(n=='desktop/files.fpa' for n,_,_ in files(g)),80)
        idle(g)
        wait_for(lambda:any(e['used'] and e['name']=='desktop/files.fpa' for e in entries(img.read_bytes())),80)
        snapshot=img.read_bytes();saved=next(e for e in entries(snapshot) if e['used'] and e['name']=='desktop/files.fpa')
        assert snapshot[saved['start']*512+4]==(1 if memory==4 else 2),'An archive source was not added'
        g.shot('archive-saved');close_checked(g,'Archive Manager')
        g.command('apptest file desktop/files.fpa');wait_for(lambda:g.window('Archive Manager'))
        idle(g);w=g.window('Archive Manager');appclick(g,'Archive Manager',205,w['h']-25-44);pick(g,'',True)
        wait_for(lambda:any(n=='first.txt' for n,_,_ in files(g)),80)
        if memory>4:wait_for(lambda:any(n=='second.txt' for n,_,_ in files(g)),80)
        idle(g)
        g.shot('archive-extracted');close_checked(g,'Archive Manager')
        g.command('apptest read '+editor_name);g.wait('APPTEST PASS loaded file opener',80);wait_for(lambda:g.window('Editor'))
        g.key('end');g.key('backspace');g.text('Z');g.key('ctrl+s');idle(g)
        g.shot('editor-128k');close_checked(g,'Editor')
        g.command('apptest finish');g.wait('APPTEST DONE: 0 failures')
    except Exception:
        g.shot('failure');print(g.serial()[-1800:],flush=True);raise
    finally:g.close()
    raw=img.read_bytes();table={e['name']:e for e in entries(raw) if e['used']}
    def content(name):
        e=table[name];return raw[e['start']*512:e['start']*512+e['size']]
    assert content('first.txt')==source
    if memory>4:
        assert content('second.txt')==second
        assert len(content('desktop/files.fpa'))>131072
    else:assert len(content('desktop/files.fpa'))<=131072
    assert content(editor_name)==editor_source[:-1]+b'Z'
    print(f'PASS {memory} MB archive save/reopen/extract; exact bytes and file limits',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--memory',type=int,default=8);run(p.parse_args().memory)
