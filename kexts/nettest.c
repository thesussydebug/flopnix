/* Exposes network state to the guest test runner. */
#include "kapi.h"
static const Kapi *api;
static void say(const char *s)
{
    for (; *s; s++) {
        int guard = 200000;
        while (!(api->inb(0x3fd) & 0x20) && --guard) ;
        api->outb(0x3f8, (u8)*s);
    }
}
static void snapshot(void)
{
    char b[160];
    api->kfmt(b, sizeof b, "NETTEST ip=%x mask=%x gw=%x dns=%x dhcp=%u\n",
              api->net_get(NET_IP), api->net_get(NET_MASK), api->net_get(NET_GW),
              api->net_get(NET_DNS), api->net_get(NET_DHCP_OK));
    say(b);
}
static int samples;
static void sample(void *unused)
{
    (void)unused;
    if (samples++ < 35) snapshot();
}
const KextHeader kext_header = {KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_KERNEL, 0, "Net test"};
int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;
    api->outb(0x3f9, 0); api->outb(0x3fb, 0x80);
    api->outb(0x3f8, 1); api->outb(0x3f9, 0);
    api->outb(0x3fb, 3); api->outb(0x3fa, 0xc7); api->outb(0x3fc, 0x0b);
    snapshot();
    if (api->net_get(NET_DHCP_OK)) {
        say(api->net_dhcp(1200) ? "NETTEST reacquire=ok\n" : "NETTEST reacquire=failed\n");
        snapshot();
        int ping = api->net_ping(api->net_get(NET_GW), 300);
        say(ping >= 0 ? "NETTEST gateway=ok\n" : "NETTEST gateway=failed\n");
    }
    if (api->cfg->net_mode == 1 && api->net_get(NET_IP)) {
        int ping = api->net_ping(api->net_get(NET_GW),300);
        say(ping >= 0 ? "NETTEST static-gateway=ok\n" : "NETTEST static-gateway=failed\n");
        snapshot();
    }
    char detail[100];
    api->kfmt(detail,sizeof detail,"NETTEST adapter=%u io=%x dns2=%x mtu=%u\n",
        api->net_get(NET_ADAPTER),api->net_get(NET_IO),api->net_get(NET_DNS2),api->net_get(NET_MTU));
    say(detail);
    api->timer_add(100, sample, 0);
    say("NETTEST READY\n");
    return 0;
}
