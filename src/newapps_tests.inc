/* Tests app logic shared by host and guest runners. */
#include "charmap_core.inc"
#include "merge_core.inc"
#include "base_core.inc"
#include "archive_core.inc"
#include "textweb_core.inc"
#include "breakout_core.inc"

static void t_newapps(void)
{
    APP_TEST_PHASE("Breakout");
    Breakout ball;bo_new(&ball);
    CHECK(ball.lives==3&&ball.left==40&&ball.level==1&&!ball.over);
    bo_paddle(&ball,-100);CHECK(ball.paddle==0);bo_paddle(&ball,999);CHECK(ball.paddle==BO_W-BO_PW);
    bo_new(&ball);ball.x=3*256;ball.y=120*256;ball.vx=-384;ball.vy=-512;
    CHECK(bo_step(&ball)==0&&ball.vx>0);
    bo_new(&ball);ball.x=144*256;ball.y=184*256;ball.vx=128;ball.vy=512;
    CHECK(bo_step(&ball)==0&&ball.vy<0&&ball.lives==3);
    bo_new(&ball);ball.x=32*256;ball.y=90*256;ball.vx=0;ball.vy=-768;
    CHECK(bo_step(&ball)==0&&!ball.brick[32]&&ball.left==39&&ball.score==10&&ball.vy>0);
    bo_new(&ball);for(int i=1;i<40;i++)ball.brick[i]=0;ball.left=1;
    ball.x=32*256;ball.y=34*256;ball.vx=0;ball.vy=-768;
    CHECK(bo_step(&ball)==2&&ball.level==2&&ball.left==40&&ball.brick[0]==2);
    for(int i=2;i>=0;i--){ball.x=3*256;ball.y=212*256;ball.vy=512;CHECK(bo_step(&ball)==1&&ball.lives==i);}
    CHECK(ball.over);CHECK(bo_step(&ball)==0&&ball.lives==0);
    bo_new(&ball);
    for(int i=0;i<5000;i++){
        bo_paddle(&ball,ball.x/256-BO_PW/2);bo_step(&ball);
        CHECK(ball.x>=BO_R*256&&ball.x<=(BO_W-BO_R)*256);
        CHECK(ball.left>=1&&ball.left<=40&&ball.lives>=0&&ball.lives<=3);
        if(ball.over)bo_new(&ball);
    }
    APP_TEST_PHASE("glyphs");
    for (int i = 0; i < 256; i++)
        CHECK(cm_hit(12 + i % 16 * 20 + 10, 40 + i / 16 * 20 + 10, 12, 40, 20, 20) == i);
    CHECK(cm_hit(11, 40, 12, 40, 20, 20) == -1);
    CHECK(cm_hit(12, 39, 12, 40, 20, 20) == -1);
    CHECK(cm_hit(332, 40, 12, 40, 20, 20) == -1);
    CHECK(cm_hit(12, 360, 12, 40, 20, 20) == -1);
    APP_TEST_PHASE("2048");

    for (int n = 0; n < 625; n++) {
        int q = n, used = 0, o = 0; u8 r[4], compact[4], want[4] = {0}; u32 score = 0;
        for (int i = 0; i < 4; i++) { r[i] = q % 5; q /= 5; if (r[i]) compact[used++] = r[i]; }
        for (int i = 0; i < used; i++) {
            u8 v = compact[i];
            if (i + 1 < used && compact[i + 1] == v) { v++; i++; score += 1u << v; }
            want[o++] = v;
        }
        CHECK(mg_row(r) == score);
        for (int i = 0; i < 4; i++) CHECK(r[i] == want[i]);
    }
    u8 board[16] = {1,1}, saved[16]; u32 points;
    CHECK(mg_move(board, 0, &points) && points == 4 && board[0] == 2);
    CHECK(!mg_move(board, 0, &points));
    for (int i = 0; i < 16; i++) saved[i] = board[i];
    for (int i = 0; i < 4; i++) mg_rotate(board);
    for (int i = 0; i < 16; i++) CHECK(board[i] == saved[i]);
    for (int dir = 0; dir < 4; dir++) {
        for (int i = 0; i < 16; i++) board[i] = 1;
        CHECK(mg_move(board, dir, &points) && points == 32);
    }
    for (int i = 0; i < 16; i++) board[i] = (i + i / 4) % 2 + 1;
    CHECK(!mg_available(board)); board[0] = 0; CHECK(mg_available(board));
    for (int i=0;i<16;i++) board[i]=29;
    CHECK(mg_move(board,0,&points) && points==0xffffffffu);
    APP_TEST_PHASE("bases");
    CHECK(bc_thousandths(1023,10)==999);
    CHECK(bc_thousandths(1024,10)==0);
    CHECK(bc_thousandths(1048,20)==0);
    CHECK(bc_thousandths(1049,20)==1);
    CHECK(bc_thousandths(1073741,30)==0);
    CHECK(bc_thousandths(1073742,30)==1);
    CHECK(bc_thousandths(2147484,30)==2);
    CHECK(bc_thousandths(0xffffffffu,30)==999);
    u32 v = 123; char s[40];
    CHECK(bc_parse("4294967295", 10, &v) == 1 && v == 0xffffffffu);
    CHECK(bc_parse("4294967296", 10, &v) == -2);
    CHECK(bc_parse("0xFFFFFFFF", 16, &v) == 1 && v == 0xffffffffu);
    CHECK(bc_parse("0b101", 2, &v) == 1 && v == 5);
    CHECK(bc_parse("0o377", 8, &v) == 1 && v == 255);
    CHECK(bc_parse("12x", 10, &v) == -1);
    CHECK(bc_parse("-1", 10, &v) == -1);
    CHECK(bc_parse("", 10, &v) == 0);
    CHECK(bc_parse("0x", 16, &v) == 0);
    const int bases[] = {2,8,10,16}; u32 rng = 5;
    for (int i = 0; i < 1000; i++) {
        rng = rng * 1664525u + 1013904223u;
        for (int b = 0; b < 4; b++) {
            bc_format(rng, bases[b], s);
            CHECK(bc_parse(s, bases[b], &v) == 1 && v == rng);
        }
    }
    APP_TEST_PHASE("archives");
    CHECK(ar_name("readme.txt")); CHECK(!ar_name("../a")); CHECK(!ar_name("a/b"));
    CHECK(!ar_name("a\\b")); CHECK(!ar_name(".")); CHECK(!ar_name("a:"));
    CHECK(!ar_name("name ")); CHECK(!ar_name(""));
    APP_TEST_PHASE("archive empty");
    u8 container[160], raw[32], unpacked[32]; ArEntry items[AR_FILES];
    for (int i=0; i<160; i++) ((volatile u8 *)container)[i]=0;
    ar_empty(container); CHECK(ar_index(container, 8, items) == 0);
    const char *name = "test.txt";
    for (int i = 0; name[i]; i++) container[8+i] = (u8)name[i];
    for (int i = 0; i < 32; i++) raw[i] = 'a';
    APP_TEST_PHASE("archive pack");
    u32 packed = lz_pack(raw, 32, container+36, 124);
    APP_TEST_PHASE("archive index");
    lz_put32(container+32, packed); container[4] = 1;
    CHECK(ar_index(container, 36+packed, items) == 1);
    CHECK(items[0].raw == 32 && items[0].offset == 36);
    APP_TEST_PHASE("archive unpack");
    CHECK(lz_unpack(container+items[0].offset, items[0].packed, unpacked, 32) == 32);
    CHECK(lz_unpack(container+36,packed+1,unpacked,32)==-1);
    container[46]=2;CHECK(lz_unpack(container+36,packed,unpacked,32)==-1);container[46]=0;
    container[47]=1;CHECK(lz_unpack(container+36,packed,unpacked,32)==-1);container[47]=0;
    CHECK(ar_index(container, 35+packed, items) == -1);
    container[8] = '/'; CHECK(ar_index(container, 36+packed, items) == -1); container[8] = 't';
    lz_put32(container+32, 0xffffffffu); CHECK(ar_index(container, 36+packed, items) == -1);
    APP_TEST_PHASE("URLs");
    TwUrl url; char dest[TW_URL];
    CHECK(tw_url("gopher://example.org/1hello%20world", &url));
    CHECK(url.port == 70 && url.type == '1' && url.path[5] == ' ');
    CHECK(tw_url("http://127.0.0.1:8080/a", &url) && url.port == 8080 && url.http);
    CHECK(!tw_url("https://example.org", &url));
    CHECK(!tw_url("gopher://host:0/", &url));
    CHECK(!tw_url("gopher://host:65536/", &url));
    CHECK(!tw_url("gopher://host/1%0d%0ax", &url));
    CHECK(!tw_url("http://host/a\r\nInjected", &url));
    CHECK(tw_url("gopher://host/7find%09two%20words", &url) && tw_eq(url.path,"find\ttwo words"));
    CHECK(!tw_url("gopher://host/0%00",&url));
    CHECK(!tw_url("gopher://host/0%09",&url));
    CHECK(!tw_url("http://user@host/",&url));
    CHECK(tw_resolve("http://host/a/b", "../c", dest));
    CHECK(tw_eq(dest, "http://host/c"));
    CHECK(tw_resolve("http://host/a/b", "/test", dest) && tw_eq(dest, "http://host/test"));
    APP_TEST_PHASE("rendering");
    static TwPage page;
    tw_render(&page, "<p>Hello &amp; world</p><script>bad()</script><a href='next'>Next</a>", 1, "http://host/a");
    CHECK(page.links == 1 && tw_eq(page.link[0].url, "http://host/next"));
    CHECK(tw_contains(page.text, "Hello & world") && !tw_contains(page.text, "bad()"));
    tw_render(&page, "1Menu\t/abc\thost\t70\r\n0Read\t/doc\thost\t70\r\niInfo\tfake\tbad\t0\r\n.\r\n", 2, "gopher://host/1");
    CHECK(page.links == 2 && tw_eq(page.link[0].url, "gopher://host:70/1/abc"));
    CHECK(tw_contains(page.text, "Info"));
    tw_render(&page, "..dot\r\n.\r\nignored", 3, "gopher://host/0");
    CHECK(tw_contains(page.text, ".dot") && !tw_contains(page.text, "ignored"));
    CHECK(tw_wrap("hello world", 0, 7) == 6);
    CHECK(tw_wrap("abcdefgh", 0, 3) == 3);
    CHECK(tw_wrap("hello world",0,5)==6);

    static struct {u32 before;TwPage p;u32 after;} guarded;
    static char fuzz[1024];u32 seed=42;
    static const char alphabet[]="<>/'\"&;#-\t\r\nabc0123 ";
    for(int trial=0;trial<300;trial++){
        for(int i=0;i<1023;i++){seed=seed*1664525u+1013904223u;fuzz[i]=alphabet[(seed>>16)%(sizeof alphabet-1)];}
        fuzz[1023]=0;guarded.before=0x12345678;guarded.after=0x87654321;
        tw_render(&guarded.p,fuzz,trial%4,"http://host/");
        CHECK(guarded.before==0x12345678&&guarded.after==0x87654321);
        CHECK(guarded.p.len>=0&&guarded.p.len<TW_TEXT&&guarded.p.links<=TW_LINKS);
        int off=0;while(guarded.p.text[off]){int next=tw_wrap(guarded.p.text,off,trial%80+1);CHECK(next>off&&next<=guarded.p.len);off=next;}
    }
}
