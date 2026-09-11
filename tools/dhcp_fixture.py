'Serve DHCP on the isolated Ethernet link used by network tests.'
import socket
import struct
import threading

SERVER_MAC = bytes.fromhex('525400123456')
SERVER_IP = bytes([192, 168, 76, 2])
CLIENT_IP = bytes([192, 168, 76, 100])

def checksum(p):
    if len(p) & 1:
        p += b'\0'
    total = sum(struct.unpack('!' + 'H' * (len(p) // 2), p))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535

class Peer:
    def __init__(self, offline=False):
        self.offline = offline
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.listener.listen(1)
        self.listener.settimeout(.25)
        self.port = self.listener.getsockname()[1]
        self.stop = threading.Event()
        self.events = []
        self.error = None
        self.discovers = self.acquisitions = self.renewals = 0
        self.nak = False
        self.conn = None
        self.thread = threading.Thread(target=self.work, daemon=True)
        self.thread.start()

    def close(self):
        self.stop.set()
        self.thread.join(2)
        self.listener.close()

    def send(self, frame):
        self.conn.sendall(struct.pack('!I', len(frame)) + frame)

    def ip_send(self, dst_mac, dst_ip, protocol, payload):
        ip = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(payload),
                                   0, 0, 64, protocol, 0, SERVER_IP, dst_ip))
        struct.pack_into('!H', ip, 10, checksum(ip))
        self.send(dst_mac + SERVER_MAC + b'\x08\x00' + ip + payload)

    def reply(self, request, kind):
        p = bytearray(240)
        p[0:3] = bytes([2, 1, 6])
        p[4:8] = request[4:8]
        p[16:20] = CLIENT_IP if kind != 6 else bytes(4)
        p[28:34] = request[28:34]
        p[236:240] = bytes([99, 130, 83, 99])
        p += bytes([53, 1, kind, 54, 4]) + SERVER_IP
        if kind == 2:
            p += bytes([1, 4, 255, 255, 255, 0, 3, 4]) + SERVER_IP
            p += bytes([6, 4, 192, 168, 76, 3])
        if kind != 6:
            p += bytes([51, 4]) + struct.pack('!I', 10)
            p += bytes([58, 4]) + struct.pack('!I', 3)
            p += bytes([59, 4]) + struct.pack('!I', 6)
        p += bytes([255])
        p += bytes(max(0, 300 - len(p)))
        udp = struct.pack('!HHHH', 67, 68, 8 + len(p), 0) + p
        self.ip_send(bytes([255]) * 6, bytes([255]) * 4, 17, udp)

    def handle(self, frame):
        if self.offline:
            return
        if len(frame) < 42:
            return
        if frame[12:14] == b'\x08\x06':
            arp = frame[14:42]
            if arp[6:8] == b'\0\1' and arp[24:28] == SERVER_IP:
                reply = arp[:6] + b'\0\2' + SERVER_MAC + SERVER_IP + arp[8:18]
                self.send(arp[8:14] + SERVER_MAC + b'\x08\x06' + reply)
            return
        if frame[12:14] != b'\x08\x00':
            return
        ip = frame[14:]
        ihl = (ip[0] & 15) * 4
        payload = ip[ihl:]
        if ip[9] == 1 and payload[0] == 8:
            pong = bytearray(payload)
            pong[0] = pong[2] = pong[3] = 0
            struct.pack_into('!H', pong, 2, checksum(pong))
            self.ip_send(frame[6:12], ip[12:16], 1, pong)
        if ip[9] != 17 or payload[:4] != b'\x00D\x00C':
            return
        p = payload[8:]
        assert len(p) >= 300
        opts = {}
        i = 240
        while i < len(p) and p[i] != 255:
            code = p[i]
            i += 1
            if code == 0:
                continue
            size = p[i]
            i += 1
            opts[code] = p[i:i + size]
            i += size
        kind = opts[53][0]
        if kind == 1:
            self.discovers += 1
            self.events.append('discover')
            if self.discovers == 1:
                self.events.append('drop-discover')
                return
            self.reply(p, 2)
        elif kind == 3 and p[12:16] == bytes(4):
            assert opts[50] == CLIENT_IP and opts[54] == SERVER_IP
            if self.renewals >= 2 and not self.nak:
                self.nak = True
                self.events.append('nak')
                self.reply(p, 6)
            else:
                self.acquisitions += 1
                self.events.append('acquire')
                self.reply(p, 5)
        elif kind == 3:
            assert p[12:16] == CLIENT_IP and 50 not in opts and 54 not in opts
            assert ip[12:16] == CLIENT_IP
            if ip[16:20] == SERVER_IP:
                self.renewals += 1
                self.events.append('renew')
                if self.renewals == 1:
                    self.events.append('renew-ack')
                    self.reply(p, 5)
            else:
                assert ip[16:20] == bytes([255]) * 4
                self.events.append('rebind')

    def work(self):
        try:
            while not self.stop.is_set():
                try:
                    self.conn, _ = self.listener.accept()
                    break
                except socket.timeout:
                    pass
            if not self.conn:
                return
            with self.conn:
                self.conn.settimeout(.25)
                buf = b''
                while not self.stop.is_set():
                    try:
                        chunk = self.conn.recv(65536)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    buf += chunk
                    while len(buf) >= 4:
                        n = struct.unpack('!I', buf[:4])[0]
                        if len(buf) < 4 + n:
                            break
                        self.handle(buf[4:4 + n])
                        buf = buf[4 + n:]
        except (ConnectionResetError, BrokenPipeError):
            pass
        except Exception as exc:
            self.error = repr(exc)
