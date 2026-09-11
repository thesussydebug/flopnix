/* Checks IPv4 interface settings. */
#pragma once
static int nc_unicast(u32 ip)
{
    u32 first = ip & 255;
    return first && first < 224 && first != 127;
}
static u32 nc_host(u32 ip)
{
    return (ip >> 24) | ((ip >> 8) & 0xff00) | ((ip << 8) & 0xff0000) | (ip << 24);
}
static int nc_mask(u32 mask)
{
    u32 inv = ~nc_host(mask);
    return mask && !(inv & (inv + 1));
}
static int nc_host_valid(u32 ip, u32 mask)
{
    if (!nc_unicast(ip)) return 0;
    u32 inv = ~nc_host(mask), host = nc_host(ip) & inv;
    return inv <= 1 || (host && host != inv);
}
static int nc_validate(u32 ip, u32 mask, u32 gw, u32 dns, u32 dns2)
{
    if (!nc_unicast(ip)) return 1;
    if (!nc_mask(mask)) return 2;
    if (!nc_host_valid(ip, mask)) return 1;
    if (gw && (!nc_host_valid(gw, mask) || (gw & mask) != (ip & mask) || gw == ip)) return 3;
    if (dns && !nc_unicast(dns)) return 4;
    if (dns2 && !nc_unicast(dns2)) return 5;
    return 0;
}
