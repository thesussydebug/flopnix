#pragma once

static void bres_line(GPt a, GPt b, void (*plot)(void *, int, int), void *ctx)
{
    int dx = b.x - a.x, dy = b.y - a.y;
    int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int err = dx - dy, x = a.x, y = a.y;
    for (;;) {
        plot(ctx, x, y);
        if (x == b.x && y == b.y) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
}
