import argparse,shutil,struct
from desktoptests import Guest,ROOT
from fscp import do_copy
from newappstests import wait_for,openapp,appclick
from archivelimitgui import close_checked

def free_heap(g):
    address=g.value('freelist');total=0;seen=set()
    while address:
        assert address not in seen;seen.add(address)
        magic,size,nxt,available=struct.unpack('<4I',g.memory(address,16));assert magic==0x48454150
        if available:total+=size
        address=nxt
    return total

def run(memory):
    folder=ROOT/'out/heap-recovery'/f'{memory}mb';folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    do_copy(str(img),str(ROOT/'kexts/newappstest.kx'),'sys/apptest.kx')
    seed=folder/'autoexec';seed.write_bytes(b'apptest\n');do_copy(str(img),str(seed),'autoexec')
    b=bytearray(img.read_bytes());struct.pack_into('<IBBBB',b,256*512,0x47464346,1,0,2,0);img.write_bytes(b)
    g=Guest(folder,img,'pentium2',memory)
    try:
        g.wait('APPTEST READY',80);wait_for(lambda:g.window('Terminal'))
        for i in range(3):
            openapp(g,'Crash Test');before=free_heap(g)
            appclick(g,'Crash Test',110,310);wait_for(lambda:free_heap(g)<64)
            close_checked(g,'Crash Test');wait_for(lambda:free_heap(g)==before)
            print(f'PASS cycle {i+1}: all {before} free bytes restored',flush=True)
        assert g.value('fault_recoveries')==0;g.shot('recovered')
    except Exception:
        g.shot('failure');print(g.serial()[-1400:],flush=True);raise
    finally:g.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--memory',type=int,default=8);run(p.parse_args().memory)
