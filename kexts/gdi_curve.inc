#pragma once

#define CURVE_MAXSEG 64

static inline GPt bez_cubic(GPt p0, GPt p1, GPt p2, GPt p3, fx t)
{
    fx u = FX_ONE - t;
    fx uu = fx_mul(u, u), tt = fx_mul(t, t);
    fx w0 = fx_mul(uu, u);
    fx w1 = 3 * fx_mul(uu, t);
    fx w2 = 3 * fx_mul(u, tt);
    fx w3 = fx_mul(tt, t);
    GPt r;
    r.x = (w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x + 32768) >> 16;
    r.y = (w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y + 32768) >> 16;
    return r;
}

static inline GPt bez_quad(GPt p0, GPt p1, GPt p2, fx t)
{
    fx u = FX_ONE - t;
    fx w0 = fx_mul(u, u);
    fx w1 = 2 * fx_mul(u, t);
    fx w2 = fx_mul(t, t);
    GPt r;
    r.x = (w0 * p0.x + w1 * p1.x + w2 * p2.x + 32768) >> 16;
    r.y = (w0 * p0.y + w1 * p1.y + w2 * p2.y + 32768) >> 16;
    return r;
}

static int flatten_cubic(GPt p0, GPt p1, GPt p2, GPt p3, GPt *out, int cap, int segs)
{
    if (segs < 1) segs = 1;
    if (segs > CURVE_MAXSEG) segs = CURVE_MAXSEG;
    if (cap < segs + 1) return -1;
    for (int i = 0; i <= segs; i++)
        out[i] = bez_cubic(p0, p1, p2, p3, (fx)((i * FX_ONE) / segs));
    return segs + 1;
}
