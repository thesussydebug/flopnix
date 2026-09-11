'Run desktop checks on disposable QEMU images.'
import argparse,json,pathlib,shutil,socket,struct,subprocess,time
from fscp import do_copy,entries,TABLE,ENTSZ
from ppm2png import convert
ROOT=pathlib.Path(__file__).resolve().parents[1]
QEMU='C:/Program Files/qemu/qemu-system-i386.exe'
class Guest:
    def __init__(self,folder,img,cpu,memory,network='none',capture=False,usb=None):
        self.folder=folder
        self.symbols={}
        for line in subprocess.check_output(['C:/msys64/clang64/bin/llvm-objdump.exe','-t',str(ROOT/'out/kernel.elf')],text=True).splitlines():
            p=line.split()
            try:self.symbols[p[-1]]=int(p[0],16)
            except (ValueError,IndexError):pass
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        self.log=(folder/'qemu.txt').open('w')
        self.proc=subprocess.Popen([QEMU,'-cpu',cpu,'-m',str(memory),'-display','none','-nic',network,'-vga','std',
            '-drive',f'if=floppy,format=raw,file={img}','-boot','a','-no-reboot','-no-shutdown',
            '-serial',f'file:{folder / "serial.txt"}','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']+
            (['-object',f'filter-dump,id=dump0,netdev=net0,file={folder / "network.pcap"}'] if capture else [])+
            (['-device','piix3-usb-uhci,id=uhci','-drive',f'if=none,id=stick,format=raw,file={usb}',
              '-device','usb-storage,bus=uhci.0,drive=stick'] if usb else []),
            stdout=self.log,stderr=self.log,creationflags=subprocess.CREATE_NO_WINDOW)
        for _ in range(100):
            try:self.conn=socket.create_connection(('127.0.0.1',port),.5);break
            except OSError:time.sleep(.1)
        else:self.close();raise RuntimeError('QMP unavailable')
        self.conn.settimeout(10);self.stream=self.conn.makefile('rwb');self.stream.readline();self.qmp('qmp_capabilities')
    def qmp(self,name,args=None):
        d={'execute':name}
        if args is not None:d['arguments']=args
        self.stream.write((json.dumps(d)+'\n').encode());self.stream.flush()
        while True:
            r=json.loads(self.stream.readline())
            if 'error' in r:raise RuntimeError(r)
            if 'return' in r:return r['return']
    def close(self):
        self.proc.terminate();self.proc.wait(timeout=10);self.log.close()
        if hasattr(self,'stream'):self.stream.close()
        if hasattr(self,'conn'):self.conn.close()
    def serial(self):return (self.folder/'serial.txt').read_text(errors='replace')
    def wait(self,text,seconds=60):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            if text in self.serial():return
            time.sleep(.2)
        raise AssertionError('Missing '+text+'\n'+self.serial()[-1800:])
    def memory(self,address,size):
        p=self.folder/'memory.bin';self.qmp('pmemsave',{'val':address,'size':size,'filename':str(p)})
        return p.read_bytes()
    def value(self,name):return struct.unpack('<i',self.memory(self.symbols[name],4))[0]
    def modules(self):
        result={}
        for i in range(self.value('nkexts')):
            d=self.memory(self.symbols['kexts']+i*56,56)
            result[d[:24].split(b'\0')[0].decode()]={'id':i,'size':struct.unpack_from('<I',d,44)[0],'status':struct.unpack_from('<i',d,52)[0]}
        return result
    def owner(self,w):return struct.unpack('<i',self.memory(self.symbols['reg_owner']+4*w['type'],4))[0]
    def windows(self):
        out=[]
        for i in range(16):
            d=self.memory(self.symbols['wins']+i*52,52)
            if not d[0]:continue
            x,y,w,h,title=struct.unpack_from('<iiiiI',d,4)
            text=(d[24:48] if d[48] else self.memory(title,64)).split(b'\0')[0].decode('latin1')
            out.append(dict(slot=i,type=d[1],inst=d[2],x=x,y=y,w=w,h=h,title=text))
        return out
    def window(self,title):
        for w in self.windows():
            if w['title']==title:return w
        raise AssertionError((title,self.windows()))
    def key(self,key):
        self.qmp('send-key',{'keys':[{'type':'qcode','data':k} for k in key.split('+')],'hold-time':50});time.sleep(.09)
    def text(self,text):
        table={' ':'spc','.':'dot','/':'slash','-':'minus',':':'shift+semicolon'}
        for c in text:self.key(table.get(c,('shift+'+c.lower()) if c.isupper() else c))
    def move(self,x,y):
        for _ in range(40):
            dx=x-self.value('mx');dy=y-self.value('my')
            if abs(dx)+abs(dy)<2:return
            self.qmp('input-send-event',{'events':[{'type':'rel','data':{'axis':'x','value':max(-80,min(80,dx))}},
                {'type':'rel','data':{'axis':'y','value':max(-80,min(80,dy))}}]});time.sleep(.06)
        raise AssertionError('Mouse did not reach target')
    def button(self,down,right=False):
        self.qmp('input-send-event',{'events':[{'type':'btn','data':{'button':'right' if right else 'left','down':down}}]});time.sleep(.15)
    def click(self,x,y,right=False):self.move(x,y);self.button(True,right);self.button(False,right);time.sleep(.2)
    def shot(self,name):
        time.sleep(.4);p=self.folder/(name+'.ppm');self.qmp('screendump',{'filename':str(p)});convert(str(p),str(p.with_suffix('.png')))
    def menu(self,label):
        assert self.value('ov_mouse'),'Popup did not open'
        n=self.value('m_n');items=self.memory(self.symbols['m_items'],n*24)
        labels=[items[i*24:(i+1)*24].split(b'\0')[0].decode() for i in range(n)]
        self.click(self.value('m_x')+24,self.value('m_y')+2+labels.index(label)*18+8)
    def command(self,cmd):
        terminal=self.window('Terminal')
        for _ in range(5):
            self.click(94,self.value('SH')-14);time.sleep(.3)
            top=struct.unpack('<i',self.memory(self.symbols['zord']+4*(self.value('nz')-1),4))[0]
            if top==terminal['slot']:break
        else:raise AssertionError('Terminal did not receive focus')
        self.text(cmd);self.key('ret');time.sleep(.5)

def run(a):
    folder=ROOT/'out/desktop-tests'/(f'{a.scenario}-{a.cpu}-{a.memory}mb'+('-no-gdi' if a.no_gdi else ''))
    folder.mkdir(parents=True,exist_ok=True);img=folder/'run.img';shutil.copyfile(ROOT/'built/flopnix.img',img)
    if a.scenario in ('selftest','appcore'):
        name='appcoretest' if a.scenario=='appcore' else 'selftest_small' if a.memory<=4 else 'selftest'
        do_copy(str(img),str(ROOT/'kexts'/(name+'.kx')),'sys/selftest.kx')
    else:
        do_copy(str(img),str(ROOT/'kexts/desktopfixture.kx'),'desktop/probe.kx')
        do_copy(str(img),str(ROOT/'kexts/desktoptest.kx'),'sys/zzuitest.kx')
        (folder/'broken').write_text('Invalid desktop module; boot must ignore this file.')
        do_copy(str(img),str(folder/'broken'),'desktop/broken.kx')
        (folder/'note').write_text('A desktop document.\n')
        do_copy(str(img),str(folder/'note'),'desktop/readme.txt')
        (folder/'autoexec').write_text('uitest check\nfixture\nuitest entry\nfixture timeron\nuitest background\nfixture timeroff\nuitest open Task Manager\n')
        do_copy(str(img),str(folder/'autoexec'),'autoexec')
    if a.no_gdi:
        d=bytearray(img.read_bytes())
        for e in entries(d):
            if e['used'] and e['name']=='sys/gdi.kx':d[TABLE*512+e['i']*ENTSZ+36]=0
        img.write_bytes(d)

    d=bytearray(img.read_bytes());struct.pack_into('<IBBBB',d,256*512,0x47464346,1,0,2,0);img.write_bytes(d)
    g=Guest(folder,img,a.cpu,a.memory)
    try:
        if a.scenario=='appcore':
            g.wait('APP CORES DONE:',60);assert ' 0 fail' in g.serial() and 'FAIL ' not in g.serial();print('PASS app cores',a.cpu,a.memory,flush=True);return
        if a.scenario=='selftest':
            g.wait('SELFTEST DONE:',100);assert ' fail' in g.serial() and ' 0 fail' in g.serial() and 'not ok' not in g.serial();print('PASS selftest',a.cpu,a.memory,flush=True);return
        g.wait('UITEST CHECK DONE',90);time.sleep(3)
        assert 'UITEST FAIL' not in g.serial(),g.serial()
        if a.scenario=='hold':
            g.command('uitest holdcheck');g.wait('UITEST PASS paused main callback protected',25);g.wait('UITEST HOLD END');time.sleep(1);g.command('uitest finish');g.wait('UITEST DONE: 0 failures');print('PASS callback overlap',flush=True);return
        g.shot('boot');print(g.windows(),flush=True)
        w=g.window('Task Manager');cx=w['x']+3;cy=w['y']+22
        g.click(cx+76,cy+8);g.shot('view-menu');g.menu('Modules');g.shot('modules')

        g.click(cx+24,cy+8);g.click(cx+76,cy+8);assert g.value('m_n')==3
        for _ in range(3):g.key('down')
        g.key('ret');assert not g.value('ov_mouse')

        g.click(cx+40,cy+20+34+18+2+4*16+6)
        g.click(cx+110,cy+(w['h']-25)-42);g.shot('protected-module')

        for _ in range(14):g.key('down')
        g.shot('modules-bottom')
        ch=w['h']-25;vis=(ch-148)//16
        g.click(cx+40,cy+74+(vis-1)*16+6)
        g.click(cx+110,cy+ch-39)
        assert g.modules()['desktop/probe.kx']['status']==46,'Unload button failed'
        g.shot('unloaded-module')
        g.click(cx+200,cy+ch-39)
        assert g.modules()['desktop/probe.kx']['status']==0,'Reload button failed'
        g.click(cx+110,cy+ch-39)
        g.click(cx+40,cy+ch-39);assert g.value('ov_mouse');g.shot('load-picker')
        px=(g.value('SW')-300)//2;py=(g.value('SH')-214)//2
        g.click(px+50,py+22+16+8)
        g.click(px+50,py+22+16+8)
        g.click(px+40,py+214-16)
        assert g.modules()['desktop/probe.kx']['status']==0,'Picker load failed'
        g.shot('loaded-module')

        for title in ('Terminal','Task Manager'):
            z=g.window(title);g.move(z['x']+90,z['y']+10);g.button(True);g.move(420,z['y']+10);g.button(False)
        g.click(40,26+2*56+15,True);g.menu('Properties');time.sleep(.4)
        prop=g.window('Properties');assert not g.value('ov_mouse');assert g.owner(prop)==g.modules()['sys/desktop.kx']['id'];g.shot('desktop-properties')

        g.command('uitest open Files');time.sleep(2);assert g.window('Properties')
        g.shot('files-with-properties')
        f=g.window('Files');fx=f['x']+3;fy=f['y']+22

        g.click(fx+200,fy+100,True);g.shot('files-context')
        if g.value('ov_mouse'):
            n=g.value('m_n');items=g.memory(g.symbols['m_items'],n*24)
            if b'Properties' in items:g.menu('Properties')
            else:g.key('esc')
        g.shot('file-properties')
        props=[z for z in g.windows() if z['title']=='Properties']
        assert len(props)==2,'File properties did not open independently'
        assert set(g.owner(z) for z in props)=={g.modules()['sys/desktop.kx']['id'],g.modules()['sys/files.kx']['id']}
        g.click(f['x']+70,f['y']+10);g.click(f['x']+f['w']-11,f['y']+10)
        assert len([z for z in g.windows() if z['title']=='Properties'])==2,'Closing Files closed Properties'
        g.shot('properties-after-files-close')

        g.command('uitest open Paint');time.sleep(2);p=g.window('Paint')
        g.click(p['x']+3+20,p['y']+22+8);g.shot('paint-menu');g.key('esc')
        g.command('uitest open Task Manager');time.sleep(.5);tm=g.window('Task Manager')
        g.click(tm['x']+3+24,tm['y']+22+8);g.menu('New Task...');g.shot('new-task')
        g.text('desktop/probe.kx');g.key('ret');time.sleep(.5)
        probe=g.window('Desktop Probe');g.shot('new-task-opened')
        g.click(probe['x']+probe['w']-11,probe['y']+10)
        g.command('uitest holdcheck');g.wait('UITEST PASS paused main callback protected',25);g.wait('UITEST HOLD END');time.sleep(1);
        g.command('uitest finish');g.wait('UITEST DONE: 0 failures');g.shot('finished')
        assert 'UITEST FAIL' not in g.serial(),g.serial()
        print('PASS desktop/module checks and menus',flush=True)
    except Exception:
        g.shot('failure');print(g.modules(),flush=True);print('faults',g.value('fault_recoveries'),g.memory(g.symbols['fault_hist'],192).hex(),flush=True);print(g.qmp('human-monitor-command',{'command-line':'info registers'}),flush=True);raise
    finally:g.close()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--scenario',choices=['ui','selftest','hold','appcore'],default='ui')
    p.add_argument('--cpu',default='pentium2');p.add_argument('--memory',type=int,default=32);p.add_argument('--no-gdi',action='store_true');run(p.parse_args())
