#pragma once

static int stroke_rdiv(int a, int b)
{
    return a >= 0 ? (a + b / 2) / b : -(((-a) + b / 2) / b);
}

static int stroke_seg(GPt a, GPt b, int half_w, GPt *out)
{
    int dx = b.x - a.x, dy = b.y - a.y;
    int len = (int)isqrt((u32)(dx * dx + dy * dy));
    if (len == 0) return 0;
    int ox = stroke_rdiv(-dy * half_w, len);
    int oy = stroke_rdiv(dx * half_w, len);
    out[0].x = a.x + ox; out[0].y = a.y + oy;
    out[1].x = b.x + ox; out[1].y = b.y + oy;
    out[2].x = b.x - ox; out[2].y = b.y - oy;
    out[3].x = a.x - ox; out[3].y = a.y - oy;
    return 1;
}
