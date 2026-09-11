/* A minimal app to use as a starting point. */
#include "kapi.h"

static const Kapi *api;

static void cmd_hello(const char *args)
{
    char buf[64];
    while (*args == ' ') args++;
    api->kfmt(buf, sizeof buf, "hello, %s! (from a kernel extension)\n",
              *args ? args : "world");
    api->shell_print(buf);
}

static void hello_draw(Win *w, int cx, int cy, int cw, int ch)
{
    (void)w; (void)cw; (void)ch;
    api->draw_text(cx + 12, cy + 12, "Hello from a KEXT!", C_NAVY);
    api->draw_text(cx + 12, cy + 32, "This whole window lives", C_BLACK);
    api->draw_text(cx + 12, cy + 48, "in hello.kx on the disk.", C_BLACK);
}
static void hello_csize(int inst, int *w, int *h)
{ (void)inst; *w = 240; *h = 80; }

const KextHeader kext_header = {
    KEXT_MAGIC, KAPI_VERSION, KEXT_KIND_APP, 0, "Hello"
};

int kext_entry(const Kapi *k)
{
    if (k->version < KAPI_VERSION) return 1;
    api = k;

    api->register_cmd("hello", "hello [name]  - greet from an extension",
                      cmd_hello);

    static const AppDesc d = {
        .title = "Hello", .max_inst = 1, .in_menu = 1,
        .draw = hello_draw, .client_size = hello_csize,
    };
    api->register_app(&d);
    return 0;
}
