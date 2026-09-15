/* Provides popup menus, clipboard data, and drag state. */
#include "os.h"
#include "../kexts/menushade.h"

#define MMAX   12
#define MITEM  24
#define MROWH  18

static char  m_items[MMAX][MITEM];
static int   m_n, m_x, m_y, m_w, m_h;
static void (*m_pick)(int idx, void *ctx);
static void *m_ctx;
static int m_sel, m_mx, m_my;

static void m_draw(void)
{
    panel(m_x, m_y, m_w, m_h, 0);
    menu_shade(&kapi,m_x+2,m_y+2,m_w-4,m_h-4,0);
    if (mx!=m_mx || my!=m_my) { m_sel=-1; m_mx=mx; m_my=my; }
    for (int i = 0; i < m_n; i++) {
        int iy = m_y + 2 + i * MROWH;
        int hov = m_sel>=0 ? m_sel==i : (mx >= m_x + 2 && mx < m_x + m_w - 2 &&
                  my >= iy && my < iy + MROWH);
        if (hov) menu_shade(&kapi,m_x+2,iy,m_w-4,MROWH,1);
        draw_text(m_x + 10, iy + 1, m_items[i], hov ? C_WHITE : C_BLACK);
    }
}

static void m_choose(int idx)
{
    void (*cb)(int,void *)=m_pick; void *ctx=m_ctx;
    set_overlay(0,0);
    if (idx>=0 && idx<m_n && cb)
        FAULT_GUARD(cb(idx,ctx),klog("menu: handler faulted\n"));
}
static int m_key(int key)
{
    if (key==27) set_overlay(0,0);
    else if (key==K_DOWN) m_sel=(m_sel+1)%m_n;
    else if (key==K_UP) m_sel=m_sel<=0 ? m_n-1 : m_sel-1;
    else if (key=='\r' || key=='\n') m_choose(m_sel);
    gui_dirty=1; return 1;
}

static int m_mouse(int x, int y, int ev)
{
    if (ev == EV_DRAG || ev == EV_RELEASE) return 1;
    if (ev == EV_PRESS &&
        x >= m_x+2 && x < m_x + m_w-2 && y >= m_y+2 && y < m_y + m_h-2) {
        int idx = (y - m_y - 2) / MROWH;

        m_choose(idx);
        return 1;
    }
    return 2;
}

void menu_show_native(int x, int y, const char *const *items, int n,
                      void (*pick)(int idx, void *ctx), void *ctx)
{
    if (n > MMAX) n = MMAX;
    if (n <= 0) return;
    int wide = 0;
    for (int i = 0; i < n; i++) {
        strlcpy(m_items[i], items[i], sizeof m_items[i]);
        int l = (int)strlen(m_items[i]);
        if (l > wide) wide = l;
    }
    m_n = n;
    m_w = wide * 8 + 20;
    if (m_w < 90) m_w = 90;
    m_h = n * MROWH + 4;
    if (x + m_w > SW) x = SW - m_w;
    if (y + m_h > SH - TBH) y = SH - TBH - m_h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    m_x = x;
    m_y = y;
    m_pick = pick;
    m_ctx = ctx;
    m_sel=-1; m_mx=mx; m_my=my;
    set_overlay(m_draw, m_mouse);
    set_overlay_key(m_key);
    gui_dirty = 1;
}

#include "clipboard.inc"
