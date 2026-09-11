'Check DHCP behavior using QEMU and captured packets.'
import argparse
import ipaddress
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
QEMU = pathlib.Path('C:/Program Files/qemu/qemu-system-i386.exe')

def wire_ip(ip):
    return int.from_bytes(ipaddress.ip_address(ip).packed, 'little')

def packets(path):
    data = path.read_bytes()
    off = 24
    while off + 16 <= len(data):
        size = struct.unpack_from('<I', data, off + 8)[0]
        off += 16
        yield data[off:off + size]
        off += size

def dhcp_types(path):
    types = []
    for p in packets(path):
        if len(p) < 282 or p[12:14] != b'\x08\x00' or p[23] != 17:
            continue
        ihl = (p[14] & 15) * 4
        udp = p[14 + ihl:]
        if udp[:4] not in (b'\x00D\x00C', b'\x00C\x00D'):
            continue
        if udp[:4] == b'\x00D\x00C':
            assert len(udp[8:]) >= 300, 'short BOOTP request'
        opts = udp[248:]
        i = 0
        while i < len(opts):
            code = opts[i]
            i += 1
            if code == 255:
                break
            if code == 0:
                continue
            size = opts[i]
            i += 1
            if code == 53 and size == 1:
                types.append(opts[i])
            i += size
    return types

def run(nic, cpu, subnet, lifecycle=False, offline=False, static=False, memory=64, bridge=False, isa_io=0):
    peer = None
    if lifecycle or offline:
        from dhcp_fixture import Peer
        peer = Peer(offline)
    name = f'{nic}-{cpu}-' + ('offline' if offline else 'lifecycle' if lifecycle else subnet.replace('/', '_') or 'default') + ('-static' if static else '') + ('-bridge6' if bridge else '') + f'-{memory}mb'
    folder = ROOT / 'out' / 'nettests' / name
    folder.mkdir(parents=True, exist_ok=True)
    img, log, capture = [folder / x for x in ('test.img', 'serial.txt', 'net.pcap')]
    shutil.copyfile(ROOT / 'built/flopnix.img', img)
    subprocess.run([sys.executable, str(ROOT / 'tools/fscp.py'), str(img),
                    str(ROOT / 'kexts/nettest.kx'), 'sys/zznettest.kx'], check=True,
                   stdout=subprocess.DEVNULL)
    for f in (log, capture):
        f.unlink(missing_ok=True)
    network = ipaddress.ip_network('192.168.76.0/24' if subnet == '192' else subnet or '10.0.2.0/24')
    base = int(network.network_address)
    address = lambda offset: str(ipaddress.IPv4Address(base + offset))
    ip = address(77 if static else 100 if subnet else 15)
    mask, gateway, dns = str(network.netmask), address(2), address(3)
    net = 'user,id=n0'
    if subnet:
        net += f',net={network},host={address(2)},dns={address(3)},dhcpstart={address(100)}'
    if static or isa_io:
        data = bytearray(img.read_bytes()); cfg = 256*512
        struct.pack_into('<I',data,cfg,0x47464346)
        data[cfg+4:cfg+8] = bytes([1,1 if static else 0,2,0])
        if static: struct.pack_into('<III',data,cfg+8,*(wire_ip(x) for x in (ip,mask,gateway)))
        struct.pack_into('<IIIHHBB32s',data,cfg+97,0x3154454e,wire_ip(dns),wire_ip('1.1.1.1'),isa_io,1492,5 if isa_io else 0,0,b'retro-pc')
        img.write_bytes(data)
    if peer:
        net = f'socket,id=n0,connect=127.0.0.1:{peer.port}'
    args = [str(QEMU), '-cpu', cpu, '-m', str(memory), '-display', 'none', '-boot', 'a',
            '-drive', f'if=floppy,format=raw,file={img}', '-netdev', net,
            '-device', f'{nic},netdev=n0' + (',bus=br5' if bridge else '') + (f',iobase={isa_io}' if isa_io else ''), '-serial', f'file:{log}',
            '-object', f'filter-dump,id=cap,netdev=n0,file={capture}']
    if bridge:
        bridge_args = []
        for i in range(6):
            bridge_args += ['-device', f'pci-bridge,id=br{i},chassis_nr={i+1},bus=' + (f'br{i-1}' if i else 'pci.0')]
        args[1:1] = bridge_args
    with (folder / 'qemu.txt').open('w') as err:
        vm = subprocess.Popen(args, stdout=err, stderr=err,
                              creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        try:
            end = time.monotonic() + (90 if peer else 65)
            text = ''
            while time.monotonic() < end and vm.poll() is None:
                time.sleep(.25)
                text = log.read_text(errors='replace') if log.exists() else ''
                if peer and peer.nak and peer.acquisitions >= 3 and text.count('dhcp=0') >= 2:
                    time.sleep(2)
                    text = log.read_text(errors='replace')
                    break
                if (not peer or offline) and 'NETTEST READY' in text:
                    break
        finally:
            vm.terminate()
            vm.wait(timeout=10)
            if peer:
                peer.close()
                (folder / 'events.txt').write_text('\n'.join(peer.events))
    expected = tuple(wire_ip(x) for x in (ip, mask, gateway, dns))
    rows = re.findall(r'NETTEST ip=(\w+) mask=(\w+) gw=(\w+) dns=(\w+) dhcp=(\d+)', text)
    if offline:
        assert rows and all(row == ('0', '0', '0', '0', '0') for row in rows), text
        assert dhcp_types(capture).count(1) >= 2, 'discovery was not retried'
        print(f'PASS {name}: no DHCP server, no fabricated address, discovery retried', flush=True)
        return
    assert len(rows) >= 2, f'{name}: missing guest output: {text}'
    if peer:
        assert not peer.error, peer.error
        for event in ('drop-discover', 'renew-ack', 'rebind', 'nak'):
            assert event in peer.events, f'missing {event}: {peer.events}'
        assert peer.acquisitions >= 3, f'no recovery: {peer.events}'
        assert any(row == ('0', '0', '0', '0', '0') for row in rows), f'no address withdrawal: {rows}'
        rows = [rows[0], rows[-1]]
    for row in rows:
        got = tuple(int(x, 16) for x in row[:4])

        assert got == expected and row[4] == ('0' if static else '1'), f'{name}: wrong lease {row}'
    if static:
        assert 'static-gateway=ok' in text and 'dns2=1010101 mtu=1492' in text, text
        assert not dhcp_types(capture), 'static mode sent DHCP'
        print(f'PASS {name}: saved static IPv4, DNS and gateway {ip}/{network.prefixlen}',flush=True)
        return
    assert 'reacquire=ok' in text and 'gateway=ok' in text, f'{name}: {text}'
    kinds = dhcp_types(capture)
    assert kinds.count(1) >= 2 and kinds.count(2) >= 2, f'{name}: missing discover/offer {kinds}'
    assert kinds.count(3) >= 2 and kinds.count(5) >= 2, f'{name}: missing request/ACK {kinds}'
    extra = ', renewal/rebind/expiry/NAK/recovery' if peer else ''
    print(f'PASS {name}: {ip}, gateway/DNS, runtime reacquire, packet exchange{extra}', flush=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nic', default='ne2k_pci', choices=['ne2k_pci', 'ne2k_isa', 'rtl8139', 'tulip', 'pcnet'])
    parser.add_argument('--cpu', default='pentium2', choices=['pentium2', 'qemu32'])
    parser.add_argument('--lifecycle', action='store_true', help='isolated short-lease DHCP server')
    parser.add_argument('--offline', action='store_true', help='no DHCP server; verify unconfigured address')
    parser.add_argument('--static',action='store_true')
    parser.add_argument('--subnet',default='')
    parser.add_argument('--memory',type=int,default=64)
    parser.add_argument('--bridge',action='store_true')
    parser.add_argument('--isa-io',type=lambda s:int(s,0),default=0)
    options = parser.parse_args()
    if options.offline:
        run(options.nic, options.cpu, '', offline=True)
    elif options.lifecycle:
        run(options.nic, options.cpu, '192', True)
    else:
        for subnet in ([options.subnet] if options.subnet else ('', '192')):
            run(options.nic, options.cpu, subnet, static=options.static, memory=options.memory, bridge=options.bridge, isa_io=options.isa_io)
