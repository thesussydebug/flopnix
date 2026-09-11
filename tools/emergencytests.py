'Inject crashes on disposable QEMU images and check the reports.'
import argparse, json, pathlib, re, shutil, socket, subprocess, sys, time
from ppm2png import convert
ROOT=pathlib.Path(__file__).resolve().parents[1]
QEMU='C:/Program Files/qemu/qemu-system-i386.exe'
PY='C:/msys64/clang64/bin/python.exe'
CASES={'normal':1,'formatter-fault':2,'formatter-loop':3,'draw-fault':4,'double-fault':5,'kernel-hang':6,'nmi-hang':7,'framebuffer-mapping':8,'invalid-video':9,'damaged-display-state':10,'no-timer':11,'original-page-fault':12}

SYMBOLS={}

def prepare():
    subprocess.run([sys.executable,str(ROOT/'tools/checkbuild.py')],check=True)
    symbols=SYMBOLS
    text=subprocess.check_output(['C:/msys64/clang64/bin/llvm-objdump.exe','-t',str(ROOT/'out/kernel.elf')],text=True)
    for row in text.splitlines():
        parts=row.split()
        if len(parts)>3:
            try: symbols[parts[-1]]=int(parts[0],16)
            except ValueError: pass
    (ROOT/'out/emergency_test_addrs.inc').write_text(f'#define PANIC_ADDR 0x{symbols["panic"]:x}u\n#define EM_HEART_ADDR 0x{symbols["emergency_watch_start"]:x}u\n#define EM_CHECK_ADDR 0x{(symbols["em_video"]+20 if "em_video" in symbols else symbols["em_video.5"]):x}u\n#define EM_TIMER_ADDR 0x{symbols["timer_alive"]:x}u\n#define EM_ARM_ADDR 0x{symbols["fault_armed"]:x}u\n')
    wd=str(ROOT).replace('C:', '/c').replace('\\','/')
    subprocess.run(['C:/msys64/usr/bin/bash.exe','-lc',f'cd "{wd}" && bash tools/mkkext.sh kexts/emergencytest.c'],check=True)

def run(case,cpu,memory,video,usb_scratch=False):
    folder=ROOT/'out/emergency-tests'/(f'{case}-{cpu}-{memory}mb-{video}'+('-usb' if usb_scratch else ''))
    folder.mkdir(parents=True,exist_ok=True)
    img=folder/'run.img'; serial=folder/'serial.txt'
    shutil.copyfile(ROOT/'built/flopnix.img',img)
    (folder/'case.bin').write_bytes(bytes([CASES[case]]))
    for source,dest in [(ROOT/'kexts/emergencytest.kx','sys/zzpanic.kx'),(folder/'case.bin','panic.case')]:
        subprocess.run([PY,str(ROOT/'tools/fscp.py'),str(img),str(source),dest],check=True,stdout=subprocess.DEVNULL)
    if case=='framebuffer-mapping' and video!='std': raise ValueError('Framebuffer mapping test needs an LFB')
    if video=='vga':
        data=bytearray(img.read_bytes()); off=256*512
        data[off:off+8]=bytes.fromhex('4643464704000200')
        img.write_bytes(data)
    serial.unlink(missing_ok=True)
    with socket.socket() as s: s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    args=[QEMU,'-cpu',cpu,'-m',str(memory),'-display','none','-vga','cirrus' if video=='cirrus' else 'std',
          '-drive',f'if=floppy,format=raw,file={img}','-boot','a','-nic','none','-no-reboot','-no-shutdown',
          '-serial',f'file:{serial}','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
    usbimg=folder/'usb.img'
    if usb_scratch:
        from usb import make
        make(str(usbimg),10)
        args += ['-usb','-drive',f'if=none,id=panicusb,format=raw,file={usbimg}','-device','usb-storage,drive=panicusb']
    with (folder/'qemu.txt').open('w') as log:
        vm=subprocess.Popen(args,stdout=log,stderr=log,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            for _ in range(100):
                try: conn=socket.create_connection(('127.0.0.1',port),.5); break
                except OSError: time.sleep(.1)
            else: raise RuntimeError('No QMP connection')
            conn.settimeout(5); stream=conn.makefile('rwb'); stream.readline()
            def qmp(command,args=None):
                d={'execute':command}
                if args is not None:d['arguments']=args
                stream.write((json.dumps(d)+'\n').encode());stream.flush()
                while True:
                    r=json.loads(stream.readline())
                    if 'error' in r: raise RuntimeError(r)
                    if 'return' in r:return r['return']
            qmp('qmp_capabilities')
            deadline=time.monotonic()+85; fired=None; nmi=False
            while time.monotonic()<deadline and vm.poll() is None:
                text=serial.read_text(errors='replace') if serial.exists() else ''
                if 'PANICTEST RUN' in text:
                    if fired is None:fired=time.monotonic()
                    if case=='nmi-hang' and not nmi and time.monotonic()-fired>2:qmp('inject-nmi');nmi=True
                    if case in ('normal','no-timer') and time.monotonic()-fired>4:break
                if 'Record these details, then restart.' in text:break
                time.sleep(.2)
            time.sleep(.5)
            qmp('screendump',{'filename':str(folder/'screen.ppm')})
            registers=qmp('human-monitor-command',{'command-line':'info registers'})
            (folder/'registers.txt').write_text(registers)
            assert 'HLT=1' in registers, 'Guest did not reach a terminal halt'
            eflags=int(re.search(r'EFL=([0-9a-fA-F]+)',registers)[1],16)
            assert not eflags & 0x200, 'Interrupts remain enabled after halt'
            if case not in ('normal','no-timer'):
                esp=int(re.search(r'ESP=([0-9a-fA-F]+)',registers)[1],16)
                cr3=int(re.search(r'CR3=([0-9a-fA-F]+)',registers)[1],16)
                assert SYMBOLS['emergency_stack']<=esp<SYMBOLS['emergency_stack']+8192, 'Not on emergency stack'
                assert cr3==SYMBOLS['emergency_pd'], 'Not using private emergency page tables'
            text=serial.read_text(errors='replace') if serial.exists() else ''
            if case in ('normal','no-timer'):assert 'PANICTEST RUN' in text and 'EMERGENCY KERNEL PANIC' not in text,text
            else:
                expected={'double-fault':'P8: Double fault','kernel-hang':'Kernel stopped responding','nmi-hang':'P2: Critical hardware interrupt'}.get(case,'Exception reporting failed')
                assert expected in text and 'Record these details, then restart.' in text,text
                if case=='original-page-fault': assert 'Original exception: P14' in text and 'Original memory: dead0000' in text,text
            convert(str(folder/'screen.ppm'),str(folder/'screen.png'))
            stream.close();conn.close()
        finally:
            vm.terminate();vm.wait(timeout=10)
    if usb_scratch:
        from usb import Fat16
        disk=Fat16(str(usbimg)); entry=disk.find('PANIC.TXT')
        if case in ('normal','no-timer'):
            assert entry and entry['size']>0, 'Normal panic report missing from USB'
            report=folder/'PANIC.TXT';disk.get('PANIC.TXT',str(report))
            assert 'FLOPNIX kernel panic' in report.read_text(errors='replace')
            size=disk.fatsz*512
            assert disk.data[disk.fat0:disk.fat0+size]==disk.data[disk.fat0+size:disk.fat0+2*size], 'FAT copies disagree'
        else: assert not entry, 'Emergency path wrote a panic report'
    print('PASS',folder.name,flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cases',nargs='+',choices=CASES,default=list(CASES))
    ap.add_argument('--cpu',default='pentium2',choices=['pentium2','qemu32'])
    ap.add_argument('--memory',type=int,default=32)
    ap.add_argument('--video',default='std',choices=['std','cirrus','vga'])
    ap.add_argument('--usb-scratch',action='store_true',help='verify report writes on a generated FAT16 image')
    a=ap.parse_args();prepare()
    for case in a.cases:run(case,a.cpu,a.memory,a.video,a.usb_scratch)
