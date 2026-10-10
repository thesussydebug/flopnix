static volatile u16 tn_queue[64];
static volatile u8 tn_head, tn_tail, tn_quit;
static const Win *tn_win;

static int tn_hook(int k)
{
    if (!tn_win || !api->win_is_focused((Win *)tn_win)) return 0;
    if (k == K_CLOSE_REQUEST) tn_quit = 1;
    if (k > K_RIGHT || (api->kbd_mods() & 8)) return 0;
    u8 next = (u8)((tn_head + 1) & 63);
    if (next != tn_tail) { tn_queue[tn_head] = (u16)k; tn_head = next; }
    return 1;
}

static int tn_send(const NetStreamOps *net, const u8 *data, int n)
{
    u32 start = ticks;
    while (n > 0) {
        int sent = net->write(data, n);
        if (sent < 0 || (u32)(ticks - start) > 500u) return 0;
        data += sent;
        n -= sent;
        if (n) { net->poll(); api->sleep_ms(10); }
    }
    return 1;
}

static void sh_telnet(const char *args)
{
    char host[96], line[96];
    u32 ip, port = 23;
    Term *t = t_current();
    if (sp_token(&args, host, sizeof host) <= 0) { tprint("usage: telnet <host> [port]\n"); return; }
    while (*args == ' ') args++;
    if (*args) {
        port = 0;
        while (*args >= '0' && *args <= '9' && port < 65536) port = port * 10 + (u32)(*args++ - '0');
        if (*args || !port || port > 65535) { tprint("telnet: bad port\n"); return; }
    }
    if (!t) { tprint("telnet: run it in a Terminal window\n"); return; }
    if (!net_up()) { tprint("telnet: no network device\n"); return; }
    const NetStreamOps *net = api->service_get("net.stream");
    if (!net || net->abi != NET_STREAM_ABI) { tprint("telnet: replace sys/net.kx to use this command\n"); return; }
    if (!net_parse_ip(host, &ip)) {
        ip = net_dns(host, 300);
        if (!ip) { kfmt(line, sizeof line, "telnet: cannot resolve %s\n", host); tprint(line); return; }
    }
    kfmt(line, sizeof line, "Trying %d.%d.%d.%d port %u...\n",
         ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, ip >> 24, port);
    tprint(line);
    int r = net->open(ip, (u16)port, 800);
    if (r) {
        tprint(r == -2 ? "telnet: another connection is open\n" : r == -3 ? "not enough memory\n" : "telnet: could not connect\n");
        return;
    }
    tn_win = 0;
    for (int i = 0; i < win_max(); i++) {
        const Win *w = win_slot(i);
        if (w && w->used && w->type == term_type && w->inst == (int)(t - terms)) tn_win = w;
    }
    tn_head = tn_tail = tn_quit = 0;
    if (api->register_key_hook(tn_hook)) {
        net->close();
        tprint("telnet: no free key hook\n");
        return;
    }
    kfmt(line, sizeof line, "%s - Ctrl+Q closes", host);
    t->band = 1;
    api->busy_set("telnet", line, -1);
    TnClient tn;
    memset(&tn, 0, sizeof tn);
    u8 in[256], out[96];
    int alive = 1;
    while (alive && !tn_quit) {
        int used = 0, n = net->read(in, sizeof in);
        if (n < 0) break;
        for (int i = 0; i < n && alive; i++) {
            int c = tn_byte(&tn, in[i], out, &used, sizeof out, t->cols, t->rows);
            if (c >= 0) tputc((char)c);
            if (used > (int)sizeof out - 16) { alive = tn_send(net, out, used); used = 0; }
        }
        int typed = 0;
        while (alive && tn_tail != tn_head) {
            int k = tn_queue[tn_tail];
            tn_tail = (u8)((tn_tail + 1) & 63);
            if (k == 17) { tn_quit = 1; break; }
            int m = tn_key(k, out + used);
            used += m;
            typed = 1;
            if (m && !tn.echo && (k == '\n' || k == '\b' || (k >= 32 && k < 256))) tputc((char)k);
            if (used > (int)sizeof out - 16) { alive = tn_send(net, out, used); used = 0; }
        }
        if (alive && used) alive = tn_send(net, out, used);
        net->poll();
        if (!n && !typed) api->sleep_ms(10);
    }
    api->unregister_key_hook(tn_hook);
    tn_win = 0;
    net->close();
    api->busy_end();
    t->band = 0;
    tprint("\nConnection closed.\n");
}
