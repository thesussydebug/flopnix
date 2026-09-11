static int net_restart_pending;
static const char *const adapter_names[] = {
    "Automatic", "NE2000 PCI", "Realtek RTL8139", "DEC / ADMtek Tulip", "AMD PCnet PCI", "NE2000 ISA"
};
static Rect nf_rect(int i)
{
    Rect r = {172, i < 5 ? 166 + i * 28 : 160 + (i - 5) * 48, i == 5 ? 236 : 164, 22};
    return r;
}
static int nf_editable(int i)
{
    return i >= 5 || netmode || (i >= 3 && netdraft.dns_manual);
}
static int parse_number(const char *s, int base, int *value)
{
    int n = 0; if (!*s) return 0;
    for (; *s; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10 : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99;
        if (d >= base || n > 65535 / base) return 0;
        n = n * base + d;
    }
    *value = n; return n <= 65535;
}
static int apply_network(void)
{
    u32 a[5] = {0,0,0,0,0};
    const char *errors[6] = {"", "IP: choose a usable address on your LAN.",
        "Mask: use a subnet mask such as 255.255.255.0.",
        "Router: use a different address on this subnet.",
        "DNS: enter a valid server address, or 0.0.0.0.",
        "Backup DNS: enter a valid address, or 0.0.0.0."};
    for (int i = 0; i < 5; i++) {
        if (!nf_editable(i)) continue;
        if (!fld[i][0] && i >= 2) a[i] = 0;
        else if (!net_parse_ip(fld[i], &a[i])) { strlcpy(msg, errors[i+1], sizeof msg); return 0; }
    }
    int err = netmode ? nc_validate(a[0],a[1],a[2],a[3],a[4]) :
        (a[3] && !nc_unicast(a[3]) ? 4 : a[4] && !nc_unicast(a[4]) ? 5 : 0);
    if (err) { strlcpy(msg, errors[err], sizeof msg); return 0; }
    int port, mtu;
    if (!np_hostname(fld[5])) { strlcpy(msg, "Computer name: use letters, digits and hyphens.", sizeof msg); return 0; }
    if (!parse_number(fld[6],16,&port) || (port && (port < 0x200 || port > 0x3e0 || (port & 31)))) {
        strlcpy(msg, "ISA port: 0 for auto, or 200-3e0 in steps of 20.", sizeof msg); return 0;
    }
    if (!parse_number(fld[7],10,&mtu) || mtu < 1280 || mtu > 1500) {
        strlcpy(msg, "Packet size: enter 1280-1500; normally 1500.", sizeof msg); return 0;
    }
    NetPrefs old; np_load(CFG, &old);
    int restart = old.adapter != netdraft.adapter || old.isa_io != port;
    net_restart_pending |= restart;
    int start_dhcp = !netmode && (CFG->net_mode || !api->net_get(NET_STATE));
    netdraft.dns = a[3]; netdraft.dns2 = a[4]; netdraft.isa_io = port; netdraft.mtu = mtu;
    strlcpy(netdraft.hostname, fld[5], sizeof netdraft.hostname);
    np_store(CFG, &netdraft); CFG->net_mode = netmode;
    if (netmode) {
        api->net_set(NET_DHCP_OK,0);
        api->net_set(NET_IP,CFG->ip=a[0]); api->net_set(NET_MASK,CFG->mask=a[1]); api->net_set(NET_GW,CFG->gw=a[2]);
    }
    api->net_set(NET_CONFIG,1);
    if (start_dhcp) api->net_set(NET_RENEW,1);
    strlcpy(msg, restart ? "Saved hardware choice needs a restart." : netmode ? "Manual IPv4 applied. Save to keep it." : "Automatic IPv4 enabled. Save to keep it.", sizeof msg);
    return 1;
}
static void network_key(int k)
{
    if (k == 19) { if (apply_network()) strlcpy(msg,config_save() ? (net_restart_pending ? "Saved. Restart to use the new adapter settings." : "Network settings saved.") : "Could not save to the floppy.",sizeof msg); return; }
    if (k == 27) { selfield = -1; return; }
    if (k == '\t') {
        int first = netpage == 1 ? 0 : 5, last = netpage == 1 ? 4 : 7;
        if (!netpage) { netpage = 1; first = 0; last = 4; }
        int i = selfield;
        for (int n=first; n<=last; n++) { if (++i > last || i < first) i = first; if (nf_editable(i)) { selfield = i; return; } }
        return;
    }
    if (k == '\n') { if (selfield >= 0) selfield = -1; else apply_network(); return; }
    int i = selfield; if (i < 0) return;
    if (k == 1) { fld[i][0] = 0; fldlen[i] = 0; return; }
    if (k == '\b') { if (fldlen[i]) fld[i][--fldlen[i]] = 0; return; }
    int allowed = i == 5 ? ((k >= 'a' && k <= 'z') || (k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9') || k == '-') :
                  i == 6 ? ((k >= '0' && k <= '9') || (k >= 'a' && k <= 'f') || (k >= 'A' && k <= 'F')) :
                  ((k >= '0' && k <= '9') || (i < 5 && k == '.'));
    int cap = i == 5 ? 31 : i < 5 ? 15 : 4;
    if (allowed && fldlen[i] < cap) { fld[i][fldlen[i]++] = (char)k; fld[i][fldlen[i]] = 0; }
}
static void network_mouse(int x, int y)
{
    selfield = -1;
    for (int i = 0; i < 3; i++) {
        Rect r = {16 + i * 136, 80, 130, 24};
        if (hit(r,x,y)) { netpage = i; view_scroll = 0; return; }
    }
    if (netpage == 0) {
        Rect connect = {22,302,168,26}, test = {202,302,204,26};
        if (hit(connect,x,y)) {
            if (!net_up()) strlcpy(msg,"No supported Ethernet adapter was found.",sizeof msg);
            else if (apply_network()) {
                if (!netmode) { api->net_set(NET_RENEW,1); strlcpy(msg,"Requesting an address. You can keep using the desktop.",sizeof msg); }
            }
        }
        if (hit(test,x,y)) {
            u32 router = api->net_get(NET_GW);
            if (!api->net_get(NET_IP)) strlcpy(msg,"Get an IP address before testing the router.",sizeof msg);
            else if (!router) strlcpy(msg,"No router is set. Local Ethernet can still work.",sizeof msg);
            else strlcpy(msg,api->net_ping(router,200) >= 0 ? "Router replied. Your local connection works." : "No router reply. Check the cable and IPv4 values.",sizeof msg);
        }
        return;
    }
    if (netpage == 1) {
        Rect automatic = {22,116,184,24}, manual = {216,116,190,24};
        if (hit(automatic,x,y)) { netmode = 0; return; }
        if (hit(manual,x,y)) { netmode = 1; return; }
        Rect dns = {22,314,384,24};
        if (hit(dns,x,y) && !netmode) { netdraft.dns_manual ^= 1; return; }
    } else {
        Rect adapter = {172,116,236,24};
        if (hit(adapter,x,y)) { netdraft.adapter = (netdraft.adapter + 1) % 6; return; }
    }
    for (int i = netpage == 1 ? 0 : 5; i < (netpage == 1 ? 5 : 8); i++)
        if (nf_editable(i) && hit(nf_rect(i),x,y)) { selfield = i; return; }
}
static void network_draw(int cx, int cy, int cw)
{
    const char *pages[] = {"Overview", "IPv4 & DNS", "Adapter"};
    for (int i = 0; i < 3; i++) { Rect r={16+i*136,80,130,24}; button(cx,cy,r,pages[i],netpage==i); }
    if (netpage == 0) {
        u32 kind=api->net_get(NET_ADAPTER), link=api->net_get(NET_LINK), state=api->net_get(NET_STATE);
        const char *status = !net_up() ? "No supported Ethernet adapter" : (link & NET_LINK_PHY) && !(link & NET_LINK_UP) ? "Cable unplugged or no link" :
            api->net_get(NET_IP) ? "IPv4 address ready" : state ? "Waiting for an automatic address..." : "IPv4 needs configuration";
        draw_text_clip(cx+22,cy+116,status,C_NAVY,cw-44);
        draw_text_clip(cx+22,cy+140,kind && kind<=5 ? adapter_names[kind] : "Check Adapter for supported hardware.",C_BLACK,cw-44);
        const char *names[]={"IP address", "Subnet mask", "Router", "DNS server"};
        for (int i=0;i<4;i++) {
            char ip[16]; ip_to_str(api->net_get(i==0?NET_IP:i==1?NET_MASK:i==2?NET_GW:NET_DNS),ip);
            draw_text(cx+22,cy+166+i*23,names[i],C_DARK); draw_text(cx+172,cy+166+i*23,ip,C_BLACK);
        }
        char detail[80];
        if (api->net_get(NET_DHCP_OK)) {
            u32 left=api->net_get(NET_LEASE_LEFT);
            if (left >= 3600) kfmt(detail,sizeof detail,"Automatic address: %u h %u min left",left/3600,(left%3600)/60);
            else if (left >= 60) kfmt(detail,sizeof detail,"Automatic address: %u min left",left/60);
            else kfmt(detail,sizeof detail,"Automatic address: %u sec left",left);
        }
        else strlcpy(detail,netmode ? "Manual address; router and DNS are optional." : "Automatic (DHCP) suits most home networks.",sizeof detail);
        draw_text_clip(cx+22,cy+268,detail,C_DARK,cw-44);
        Rect connect={22,302,168,26},test={202,302,204,26};
        button(cx,cy,connect,netmode?"Apply IPv4":"Reconnect",0); button(cx,cy,test,"Test router",0);
        return;
    }
    if (netpage == 1) {
        Rect automatic={22,116,184,24},manual={216,116,190,24};
        button(cx,cy,automatic,"Automatic (DHCP)",!netmode); button(cx,cy,manual,"Manual address",netmode);
        draw_text_clip(cx+22,cy+145,netmode?"Use values from your router or administrator.":"Your router supplies the address and subnet.",C_DARK,cw-44);
    } else {
        draw_text(cx+22,cy+120,"Adapter",C_BLACK);
        Rect r={172,116,236,24}; button(cx,cy,r,adapter_names[netdraft.adapter],0);
    }
    const char *labels[]={"IP address","Subnet mask","Router","DNS server","Backup DNS","Computer name","ISA port (hex)","Packet size"};
    int selectors[]={NET_IP,NET_MASK,NET_GW,NET_DNS,NET_DNS2};
    for (int i=netpage==1?0:5;i<(netpage==1?5:8);i++) {
        Rect r=nf_rect(i); int editable=nf_editable(i); char value[40];
        if (!editable) ip_to_str(api->net_get(selectors[i]),value); else strlcpy(value,fld[i],sizeof value);
        draw_text(cx+22,cy+r.y+3,labels[i],C_BLACK);
        fill_rect(cx+r.x,cy+r.y,r.w,r.h,editable?C_WHITE:C_G0+7); bevel(cx+r.x,cy+r.y,r.w,r.h,1);
        if (selfield==i && gui_blink) { int n=strlen(value); value[n]='_'; value[n+1]=0; }
        int skip = (int)strlen(value) - (r.w-8)/8;
        draw_text_clip(cx+r.x+4,cy+r.y+3,value+(skip>0?skip:0),C_BLACK,r.w-8);
    }
    if (netpage==1) {
        Rect r={22,314,384,24};
        if (!netmode) button(cx,cy,r,netdraft.dns_manual?"DNS: use my servers":"DNS: supplied automatically",netdraft.dns_manual);
        else draw_text_clip(cx+22,cy+317,"Router/DNS may be blank for a local-only LAN.",C_DARK,cw-44);
    } else {
        draw_text_clip(cx+22,cy+288,"1500 is normal; 1492 suits PPPoE routers.",C_DARK,cw-44);
        draw_text_clip(cx+22,cy+310,"Adapter / ISA port changes need a restart.",C_DARK,cw-44);
        draw_text_clip(cx+22,cy+332,"ISA port 0 = automatic. Ctrl+S saves.",C_DARK,cw-44);
    }
}
