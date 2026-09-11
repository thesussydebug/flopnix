/* Checks DHCP packets and calculates lease times. */
#pragma once

enum { DH_OFF, DH_SELECT, DH_REQUEST, DH_BOUND, DH_RENEW, DH_REBIND };
enum { DH_MASK = 1, DH_GW = 2, DH_DNS = 4, DH_LEASE = 8,
       DH_T1 = 16, DH_T2 = 32 };
typedef struct {
    u32 ip, server, mask, gw, dns, dns2, lease, t1, t2;
    u8 type, seen, overload;
} DhReply;

static inline u32 dh_ip(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static inline u32 dh_seconds(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static inline int dh_unicast(u32 ip)
{
    return (ip & 255) > 0 && (ip & 255) < 224 && (ip & 255) != 127;
}
static inline int dh_options(const u8 *p, int n, DhReply *r, int main)
{
    for (int i = 0; i < n;) {
        int code = p[i++];
        if (!code) continue;
        if (code == 255) return 1;
        if (i == n) return 0;
        int len = p[i++];
        if (len > n - i) return 0;
        const u8 *v = p + i;
        switch (code) {
        case 53:
            if (len != 1 || r->type) return 0;
            r->type = v[0]; break;
        case 52:
            if (!main || len != 1 || v[0] < 1 || v[0] > 3) return 0;
            r->overload = v[0]; break;
        case 54:
            if (len != 4 || r->server) return 0;
            r->server = dh_ip(v); break;
        case 1:
            if (len != 4) return 0;
            r->mask = dh_ip(v); r->seen |= DH_MASK; break;
        case 3: case 6:
            if (len < 4 || len % 4) return 0;
            if (code == 3) { r->gw = dh_ip(v); r->seen |= DH_GW; }
            else { r->dns = dh_ip(v); r->dns2 = len >= 8 ? dh_ip(v + 4) : 0; r->seen |= DH_DNS; }
            break;
        case 51: case 58: case 59:
            if (len != 4) return 0;
            if (code == 51) { r->lease = dh_seconds(v); r->seen |= DH_LEASE; }
            if (code == 58) { r->t1 = dh_seconds(v); r->seen |= DH_T1; }
            if (code == 59) { r->t2 = dh_seconds(v); r->seen |= DH_T2; }
            break;
        }
        i += len;
    }
    return 1;
}

static inline int dh_parse(const u8 *p, int n, u32 xid, const u8 *mac, DhReply *r)
{
    r->ip = r->server = r->mask = r->gw = r->dns = r->dns2 = 0;
    r->lease = r->t1 = r->t2 = 0;
    r->type = r->seen = r->overload = 0;
    if (n < 240 || p[0] != 2 || p[1] != 1 || p[2] != 6 || dh_ip(p + 4) != xid)
        return 0;
    for (int i = 0; i < 6; i++) if (p[28 + i] != mac[i]) return 0;
    if (p[236] != 99 || p[237] != 130 || p[238] != 83 || p[239] != 99) return 0;
    r->ip = dh_ip(p + 16);
    if (!dh_options(p + 240, n - 240, r, 1)) return 0;
    if ((r->overload & 1) && !dh_options(p + 108, 128, r, 0)) return 0;
    if ((r->overload & 2) && !dh_options(p + 44, 64, r, 0)) return 0;
    if (!dh_unicast(r->server)) return 0;
    if (r->type == 6) return 1;
    if ((r->type != 2 && r->type != 5) || !dh_unicast(r->ip)) return 0;
    if (r->type == 5 && (!(r->seen & DH_LEASE) || !r->lease)) return 0;
    return 1;
}

static inline int dh_accept(int state, u32 ip, u32 server, const DhReply *r)
{
    if (state == DH_SELECT) return r->type == 2;
    if (state != DH_REQUEST && state != DH_RENEW && state != DH_REBIND) return 0;
    if (state != DH_REBIND && r->server != server) return 0;
    return r->type == 6 || (r->type == 5 && r->ip == ip);
}

static inline int dh_lease_phase(u32 age, u32 lease, u32 t1, u32 t2)
{
    if (lease == 0xffffffff) return DH_BOUND;
    if (age >= lease) return DH_OFF;
    if (age >= t2) return DH_REBIND;
    if (age >= t1) return DH_RENEW;
    return DH_BOUND;
}

static inline void dh_put_ip(u8 *p, u32 v)
{
    for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (i * 8));
}

static inline int dh_build(u8 *p, int cap, int state, u32 xid, const u8 *mac,
                           u32 ip, u32 server, u32 secs)
{
    if (cap < 300) return 0;
    for (int i = 0; i < 300; i++) p[i] = 0;
    p[0] = 1; p[1] = 1; p[2] = 6;
    dh_put_ip(p + 4, xid);
    if (secs > 65535) secs = 65535;
    p[8] = (u8)(secs >> 8); p[9] = (u8)secs;
    if (state != DH_RENEW) p[10] = 0x80;
    if (state == DH_RENEW || state == DH_REBIND) dh_put_ip(p + 12, ip);
    for (int i = 0; i < 6; i++) p[28 + i] = mac[i];
    p[236] = 99; p[237] = 130; p[238] = 83; p[239] = 99;
    int n = 240;
    p[n++] = 53; p[n++] = 1; p[n++] = state == DH_SELECT ? 1 : 3;
    p[n++] = 61; p[n++] = 7; p[n++] = 1;
    for (int i = 0; i < 6; i++) p[n++] = mac[i];
    p[n++] = 55; p[n++] = 6;
    p[n++] = 1; p[n++] = 3; p[n++] = 6; p[n++] = 51; p[n++] = 58; p[n++] = 59;
    p[n++] = 57; p[n++] = 2; p[n++] = 2; p[n++] = 64;
    if (state == DH_REQUEST) {
        p[n++] = 50; p[n++] = 4; dh_put_ip(p + n, ip); n += 4;
        p[n++] = 54; p[n++] = 4; dh_put_ip(p + n, server); n += 4;
    }
    p[n] = 255;
    return 300;
}

static inline int dh_hostname(u8 *p, int n, int cap, const char *name)
{
    int len=0; while (name[len] && len<31) len++;
    if (!len) return n;
    int pos=240;
    while (pos<n && p[pos]!=255) {
        int code=p[pos++]; if (!code) continue;
        if (pos>=n || p[pos]>n-pos-1) return 0;
        pos += 1+p[pos];
    }
    if (pos>=n || pos+len+3>cap) return 0;
    p[pos++]=12; p[pos++]=(u8)len;
    for (int i=0;i<len;i++) p[pos++]=(u8)name[i];
    p[pos++]=255;
    return pos>n ? pos : n;
}
