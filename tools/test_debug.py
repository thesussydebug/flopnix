import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from fscp import do_copy
from usb import make, Fat16

root = Path(__file__).resolve().parents[1]
os.chdir(root)
qemu = next((str(p) for p in (Path('C:/msys64/clang64/bin/qemu-system-i386.exe'), Path('C:/Program Files/qemu/qemu-system-i386.exe')) if p.exists()), shutil.which('qemu-system-i386'))
assert qemu, 'QEMU required'
probe = root/'tools/debugtest.kx'
assert probe.exists(), 'Build tools/debugtest.c with tools/mkkext.sh'
memory = os.environ.get('FLOPNIX_TEST_MEMORY', '32')
kernel = (root/'built/flopnix.ku').read_bytes()
class KernelHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != '/kernel.ku':
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Length', str(len(kernel)))
        self.send_header('Content-Type', 'application/octet-stream')
        self.end_headers()
        self.wfile.write(kernel)
    def log_message(self, *args):
        pass
for nic in sys.argv[1:] or ['ne2k_pci']:
    folder = root/'out'/f'debug-{nic}-{memory}'
    folder.mkdir(parents=True, exist_ok=True)
    floppy, usb, serial = (folder/n for n in ('floppy.img', 'usb.img', 'serial.txt'))
    shutil.copyfile(root/'built/flopnix.img', floppy)
    do_copy(str(floppy), str(probe), 'sys/debugtest.kx')
    do_copy(str(floppy), str(root/'built/flopnix.ku'), 'probe.ku')
    make(str(usb), 10)
    server = HTTPServer(('127.0.0.1', 0), KernelHandler)
    port = server.server_port
    port_file = folder/'http-port'
    port_file.write_bytes(struct.pack('<H',port))
    do_copy(str(floppy), str(port_file), 'http-port')
    threading.Thread(target=server.serve_forever, daemon=True).start()
    serial.write_text('')
    args = [qemu, '-cpu', 'pentium2', '-m', memory, '-boot', 'a',
            '-drive', f'if=floppy,format=raw,file={floppy.as_posix()}',
            '-device', 'piix3-usb-uhci,id=uhci',
            '-drive', f'if=none,id=stick,format=raw,file={usb.as_posix()}',
            '-device', 'usb-storage,bus=uhci.0,drive=stick',
            '-netdev', 'user,id=n0', '-device', f'{nic},netdev=n0',
            '-display', 'none', '-serial', f'file:{serial.as_posix()}']
    with (folder/'qemu.log').open('w') as log:
        process = subprocess.Popen(args, stdout=log, stderr=log,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            deadline = time.monotonic()+120
            while time.monotonic()<deadline and process.poll() is None:
                output = serial.read_text(errors='replace')
                match = re.search(r'DEBUG TEST: (\d+) pass (\d+) fail', output)
                if match:
                    print(nic+': '+output.strip(), flush=True)
                    assert int(match[2]) == 0, 'Guest tests failed'
                    break
                time.sleep(.2)
            else:
                raise AssertionError('Guest tests incomplete: '+serial.read_text(errors='replace'))
        finally:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
            server.shutdown()
            server.server_close()
    volume = Fat16(str(usb))
    names = ['NET0001.PCAP', 'NET0002.PCAP']
    if int(memory)>=16:
        names.append('NET0005.PCAP')
    for name in names:
        out = folder/name
        alias=name[:-1]
        entry=volume.find(alias)
        assert entry, alias
        lfn=bytes(volume.data[entry['off']-32:entry['off']])
        offsets=(1,3,5,7,9,14,16,18,20,22,24,28,30)
        longname=''.join(chr(struct.unpack_from('<H',lfn,o)[0]) for o in offsets).split('\0')[0]
        assert longname==name and lfn[0]==0x41 and lfn[11]==15, longname
        volume.get(alias, str(out))
        data = out.read_bytes()
        magic,major,minor,_,_,snap,link = struct.unpack_from('<IHHIIII',data)
        assert (magic,major,minor,snap,link) == (0xa1b2c3d4,2,4,1536,1)
        pos,count=24,0
        while pos<len(data):
            sec,usec,cap,orig=struct.unpack_from('<IIII',data,pos)
            assert sec>1700000000 and usec<1000000 and cap==orig and cap<=snap
            pos+=16+cap;count+=1
        assert pos==len(data) and count>0
        print(f'{name}: {count} complete Ethernet records',flush=True)
        if name=='NET0005.PCAP':
            pos,segments=24,[]
            while pos<len(data):
                size=struct.unpack_from('<I',data,pos+8)[0]
                frame=data[pos+16:pos+16+size];pos+=16+size
                if len(frame)<54 or frame[12:14]!=b'\x08\x00' or frame[23]!=6:
                    continue
                tcp=14+(frame[14]&15)*4
                if struct.unpack_from('!H',frame,tcp)[0]!=port:
                    continue
                payload=tcp+(frame[tcp+12]>>4)*4
                end=14+struct.unpack_from('!H',frame,16)[0]
                if payload<end:
                    seq=struct.unpack_from('!I',frame,tcp+4)[0]
                    segments.append((seq,frame[payload:end]))
            stream=bytearray();end=None
            for seq,payload in sorted(segments):
                if end is None:
                    end=seq
                assert seq<=end, 'Gap in captured kernel response'
                if seq+len(payload)>end:
                    stream.extend(payload[end-seq:]);end=seq+len(payload)
            assert bytes(stream).split(b'\r\n\r\n',1)[1]==kernel
            print('Captured HTTP kernel matches build byte for byte',flush=True)
        tshark=Path('C:/Program Files/Wireshark/tshark.exe')
        if tshark.exists():
            parsed=subprocess.run([str(tshark),'-r',str(out),'-T','fields','-e','frame.number'],capture_output=True,text=True,check=True)
            assert len(parsed.stdout.splitlines())==count
            if name=='NET0001.PCAP':
                icmp=subprocess.run([str(tshark),'-r',str(out),'-Y','icmp','-T','fields','-e','icmp.type'],capture_output=True,text=True,check=True)
                assert {'0','8'}.issubset(set(icmp.stdout.splitlines())),icmp.stdout
            print('Wireshark reader: PASS',flush=True)
