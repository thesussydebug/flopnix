/* Reads and writes network packet fields. */
static inline u16 nw_l4_csum(u32 src_be, u32 dst_be, int proto, const u8 *seg, int len)
{
    u32 s = 0;

    s += ((src_be & 0xFF) << 8) | ((src_be >> 8) & 0xFF);
    s += ((src_be >> 8) & 0xFF00) | (src_be >> 24);
    s += ((dst_be & 0xFF) << 8) | ((dst_be >> 8) & 0xFF);
    s += ((dst_be >> 8) & 0xFF00) | (dst_be >> 24);
    s += (u32)proto;
    s += (u32)len;
    const u8 *p = seg;
    int n = len;
    while (n > 1) { s += ((u32)p[0] << 8) | p[1]; p += 2; n -= 2; }
    if (n) s += (u32)p[0] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

static inline u16 nw_tcp_csum(u32 src, u32 dst, const u8 *seg, int len)
{
    return nw_l4_csum(src, dst, 6, seg, len);
}

static inline int nw_url_parse(const char *url, char *host, int hostcap,
                        u16 *port, char *path, int pathcap)
{
    const char *s = url;
    for (const char *p = url; *p; p++) {
        if (p[0] == ':' && p[1] == '/' && p[2] == '/') {
            if (!(p - url == 4 && url[0] == 'h' && url[1] == 't' &&
                  url[2] == 't' && url[3] == 'p'))
                return 0;
            s = p + 3;
            break;
        }
        if (*p == '/') break;
    }
    int h = 0;
    while (*s && *s != ':' && *s != '/') {
        if (h + 1 >= hostcap) return 0;
        host[h++] = *s++;
    }
    if (!h) return 0;
    host[h] = 0;
    u32 pt = 80;
    if (*s == ':') {
        s++;
        if (*s < '0' || *s > '9') return 0;
        pt = 0;
        while (*s >= '0' && *s <= '9') {
            pt = pt * 10 + (u32)(*s++ - '0');
            if (pt > 65535) return 0;
        }
        if (!pt) return 0;
    }
    *port = (u16)pt;
    int n = 0;
    if (!*s) {
        if (pathcap < 2) return 0;
        path[n++] = '/';
    } else {
        while (*s) {
            if (n + 1 >= pathcap) return 0;
            path[n++] = *s++;
        }
    }
    path[n] = 0;
    return 1;
}

static inline int nw_ip_parse(const char *s, u32 *ip_be)
{
    u32 out = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return 0;
        u32 v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (u32)(*s++ - '0');
            if (v > 255) return 0;
        }
        out |= v << (part * 8);
        if (part < 3) {
            if (*s != '.') return 0;
            s++;
        }
    }
    if (*s) return 0;
    *ip_be = out;
    return 1;
}

static inline int nw_dns_build(const char *name, u16 id, u8 *out, int cap)
{
    int nl = 0;
    while (name[nl]) nl++;
    if (!nl || nl > 253 || 12 + nl + 2 + 4 > cap) return 0;
    out[0] = (u8)(id >> 8); out[1] = (u8)id;
    out[2] = 0x01; out[3] = 0x00;
    out[4] = 0; out[5] = 1;
    out[6] = out[7] = out[8] = out[9] = out[10] = out[11] = 0;
    int w = 12;
    int lab = w++;
    out[lab] = 0;
    for (int i = 0; i < nl; i++) {
        if (name[i] == '.') {
            if (!out[lab] || out[lab] > 63) return 0;
            lab = w++;
            out[lab] = 0;
        } else {
            out[w++] = (u8)name[i];
            out[lab]++;
        }
    }
    if (!out[lab] || out[lab] > 63) return 0;
    out[w++] = 0;
    out[w++] = 0; out[w++] = 1;
    out[w++] = 0; out[w++] = 1;
    return w;
}

static inline int nw_dns_skipname(const u8 *pkt, int len, int off)
{
    int hops = 0;
    while (off < len) {
        u8 l = pkt[off];
        if (!l) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : -1;
        if ((l & 0xC0) || ++hops > 64) return -1;
        off += 1 + l;
    }
    return -1;
}

static inline u32 nw_dns_parse(const u8 *pkt, int len, u16 id)
{
    if (len < 12) return 0;
    if (pkt[0] != (u8)(id >> 8) || pkt[1] != (u8)id) return 0;
    if (!(pkt[2] & 0x80)) return 0;
    if (pkt[3] & 0x0F) return 0;
    int qd = (pkt[4] << 8) | pkt[5];
    int an = (pkt[6] << 8) | pkt[7];
    int off = 12;
    while (qd--) {
        off = nw_dns_skipname(pkt, len, off);
        if (off < 0 || off + 4 > len) return 0;
        off += 4;
    }
    while (an--) {
        off = nw_dns_skipname(pkt, len, off);
        if (off < 0 || off + 10 > len) return 0;
        int type = (pkt[off] << 8) | pkt[off + 1];
        int cls  = (pkt[off + 2] << 8) | pkt[off + 3];
        int rdl  = (pkt[off + 8] << 8) | pkt[off + 9];
        off += 10;
        if (off + rdl > len) return 0;
        if (type == 1 && cls == 1 && rdl == 4)
            return (u32)pkt[off] | ((u32)pkt[off + 1] << 8) |
                   ((u32)pkt[off + 2] << 16) | ((u32)pkt[off + 3] << 24);
        off += rdl;
    }
    return 0;
}

static inline int nw_dhcp_opt4(const u8 *opts, int len, u8 code, u8 out4[4])
{
    int i = 0;
    while (i < len) {
        u8 c = opts[i];
        if (c == 0) { i++; continue; }
        if (c == 255) return 0;
        if (i + 2 > len) return 0;
        u8 l = opts[i + 1];
        if (i + 2 + l > len) return 0;
        if (c == code && l >= 4) {
            for (int j = 0; j < 4; j++) out4[j] = opts[i + 2 + j];
            return 1;
        }
        i += 2 + l;
    }
    return 0;
}

static inline int nw_http_status(const u8 *buf, int len)
{
    if (len < 12) return -1;
    if (buf[0] != 'H' || buf[1] != 'T' || buf[2] != 'T' || buf[3] != 'P')
        return -1;
    int i = 4;
    while (i < len && buf[i] != ' ') i++;
    i++;
    if (i + 3 > len) return -1;
    int st = 0;
    for (int d = 0; d < 3; d++) {
        if (buf[i + d] < '0' || buf[i + d] > '9') return -1;
        st = st * 10 + (buf[i + d] - '0');
    }
    return st;
}

static inline int nw_http_clen(const u8 *buf, int len)
{
    static const char key[] = "content-length";
    const int klen = 14;

    for (int i = 0; i + klen < len; i++) {
        if (i && !(buf[i - 1] == '\n')) continue;
        int k = 0;
        while (k < klen) {
            u8 c = buf[i + k];
            if (c >= 'A' && c <= 'Z') c = (u8)(c + 32);
            if (c != (u8)key[k]) break;
            k++;
        }
        if (k != klen) continue;
        int j = i + klen;
        while (j < len && buf[j] == ' ') j++;
        if (j >= len || buf[j] != ':') continue;
        j++;
        while (j < len && buf[j] == ' ') j++;
        if (j >= len || buf[j] < '0' || buf[j] > '9') return -1;
        int v = 0;
        while (j < len && buf[j] >= '0' && buf[j] <= '9') {
            if (v > 200000000) return -1;
            v = v * 10 + (buf[j++] - '0');
        }

        if (j >= len) return -1;
        return v;
    }
    return -1;
}

static inline int nw_http_body(const u8 *buf, int len)
{
    for (int i = 0; i + 3 < len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' &&
            buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return i + 4;
    return -1;
}
