/* Tests DHCP packet validation and lease changes. */
static void t_dhcp_reply(void)
{
    static const u8 mac[6] = {2,3,4,5,6,7};
    static u8 p[320];
    api->memset(p, 0, sizeof p);
    p[0] = 2; p[1] = 1; p[2] = 6;
    p[4] = 0x78; p[5] = 0x56; p[6] = 0x34; p[7] = 0x12;
    p[16] = 192; p[17] = 168; p[18] = 7; p[19] = 100;
    api->memcpy(p + 28, mac, 6);
    p[236] = 99; p[237] = 130; p[238] = 83; p[239] = 99;
    static const u8 opts[] = {53,1,2,54,4,192,168,7,1,1,4,255,255,255,0,
                            3,4,192,168,7,1,6,4,192,168,7,2,
                            51,4,0,0,0x0e,0x10,255};
    api->memcpy(p + 240, opts, sizeof opts);
    DhReply r;
    CHECK(dh_parse(p, 240 + sizeof opts, 0x12345678, mac, &r));
    CHECK(r.ip == 0x6407a8c0 && r.server == 0x0107a8c0);
    CHECK(r.mask == 0x00ffffff && r.dns == 0x0207a8c0 && r.lease == 3600);
    CHECK(!dh_parse(p, 240 + sizeof opts, 0x12345679, mac, &r));
    p[28] ^= 1;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
    p[28] ^= 1;
    p[239] = 0;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
    p[239] = 99;
    p[0] = 1;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
    p[0] = 2;
    p[2] = 16;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
    p[2] = 6;
    CHECK(!dh_parse(p, 241, 0x12345678, mac, &r));
    p[241] = 0;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
    p[241] = 1;
    p[242] = 5;
    CHECK(dh_parse(p, sizeof p, 0x12345678, mac, &r));
    p[267] = 255;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
    api->memcpy(p + 240, opts, sizeof opts);
    CHECK(dh_parse(p, sizeof p, 0x12345678, mac, &r));
    CHECK(dh_accept(DH_SELECT, 0, 0, &r));
    CHECK(!dh_accept(DH_OFF, 0, 0, &r));
    CHECK(!dh_accept(DH_REQUEST, r.ip, r.server, &r));
    r.type = 5;
    CHECK(dh_accept(DH_REQUEST, r.ip, r.server, &r));
    CHECK(!dh_accept(DH_SELECT, 0, 0, &r));
    CHECK(!dh_accept(DH_REQUEST, r.ip + 1, r.server, &r));
    CHECK(!dh_accept(DH_REQUEST, r.ip, r.server + 1, &r));
    CHECK(dh_accept(DH_REBIND, r.ip, r.server + 1, &r));
    r.type = 6; r.ip = 0;
    CHECK(dh_accept(DH_REQUEST, 0x6407a8c0, r.server, &r));
    CHECK(!dh_accept(DH_REQUEST, 0x6407a8c0, r.server + 1, &r));

    p[240] = 53; p[241] = 1; p[242] = 2;
    p[243] = 52; p[244] = 1; p[245] = 1; p[246] = 255;
    api->memcpy(p + 108, opts + 3, sizeof opts - 3);
    CHECK(dh_parse(p, sizeof p, 0x12345678, mac, &r));
    CHECK(r.server == 0x0107a8c0 && r.dns == 0x0207a8c0);
    p[108] = 54; p[109] = 127;
    CHECK(!dh_parse(p, sizeof p, 0x12345678, mac, &r));
}

static void t_dhcp_lease(void)
{
    CHECK(dh_lease_phase(49, 100, 50, 87) == DH_BOUND);
    CHECK(dh_lease_phase(50, 100, 50, 87) == DH_RENEW);
    CHECK(dh_lease_phase(87, 100, 50, 87) == DH_REBIND);
    CHECK(dh_lease_phase(100, 100, 50, 87) == DH_OFF);
    CHECK(dh_lease_phase(0, 0, 0, 0) == DH_OFF);
    CHECK(dh_lease_phase(0xfffffffe, 0xffffffff, 50, 87) == DH_BOUND);
}

static void t_dhcp_build(void)
{
    static u8 p[301];
    static const u8 mac[6] = {2,3,4,5,6,7};
    p[300] = 123;
    CHECK(!dh_build(p, 299, DH_SELECT, 12, mac, 0, 0, 0));
    CHECK(dh_build(p, 300, DH_SELECT, 12, mac, 0, 0, 70000) == 300);
    CHECK(p[300] == 123 && p[0] == 1 && p[2] == 6 && p[10] == 128);
    CHECK(p[8] == 255 && p[9] == 255 && dh_ip(p + 12) == 0);
    CHECK(p[242] == 1 && p[243] == 61 && p[245] == 1 && p[251] == 7);
    u8 v[4];
    CHECK(!nw_dhcp_opt4(p + 240, 60, 50, v));
    CHECK(dh_build(p, 300, DH_REQUEST, 12, mac, 0x6407a8c0, 0x0107a8c0, 1) == 300);
    CHECK(nw_dhcp_opt4(p + 240, 60, 50, v) && dh_ip(v) == 0x6407a8c0);
    CHECK(nw_dhcp_opt4(p + 240, 60, 54, v) && dh_ip(v) == 0x0107a8c0);
    CHECK(dh_build(p, 300, DH_RENEW, 12, mac, 0x6407a8c0, 0x0107a8c0, 0) == 300);
    CHECK(p[10] == 0 && dh_ip(p + 12) == 0x6407a8c0 && p[242] == 3);
    CHECK(!nw_dhcp_opt4(p + 240, 60, 50, v) && !nw_dhcp_opt4(p + 240, 60, 54, v));
    CHECK(dh_build(p, 300, DH_REBIND, 12, mac, 0x6407a8c0, 0x0107a8c0, 0) == 300);
    CHECK(p[10] == 128 && dh_ip(p + 12) == 0x6407a8c0);
}
