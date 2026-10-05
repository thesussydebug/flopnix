#pragma once
#include "g3d.h"

#define CUBE_VERTS   24
#define CUBE_INDICES 36

static inline void cube3d_build(G3VtxN *v, u16 *idx, u32 diffuse)
{
    for (int f = 0; f < 6; f++) {
        int axis = f >> 1, u = (axis + 1) % 3, w = (axis + 2) % 3;
        fx s = (f & 1) ? -FX_ONE : FX_ONE;
        for (int k = 0; k < 4; k++) {
            G3VtxN *o = &v[f * 4 + k];
            fx a = (k == 1 || k == 2) ? FX_ONE : -FX_ONE;
            fx b = (k >= 2) ? FX_ONE : -FX_ONE;
            fx p[3], n[3];
            n[axis] = s; n[u] = 0; n[w] = 0;
            p[axis] = s; p[u] = a; p[w] = s > 0 ? b : -b;
            o->x = p[0]; o->y = p[1]; o->z = p[2];
            o->nx = n[0]; o->ny = n[1]; o->nz = n[2];
            o->diffuse = diffuse;
        }
        u16 base = (u16)(f * 4), *t = &idx[f * 6];
        t[0] = base; t[1] = (u16)(base + 1); t[2] = (u16)(base + 2);
        t[3] = base; t[4] = (u16)(base + 2); t[5] = (u16)(base + 3);
    }
}

static inline void cube3d_lit(const G3dOps *g3, G3D *c)
{
    static const G3Light sun = { { FX(2) / 5, -FX(3) / 5, -FX_ONE, 0 }, FX(3) / 4 };
    g3->set(c, G3S_CULL, G3CULL_CW);
    g3->set(c, G3S_SHADE, G3SHADE_GOURAUD);
    g3->set(c, G3S_LIGHTING, 1);
    g3->set(c, G3S_AMBIENT, FX_ONE / 4);
    g3->light(c, 0, &sun);
}

static inline int cube3d_draw(const G3dOps *g3, G3D *c, fx ax, fx ay,
                              const G3VtxN *v, const u16 *idx)
{
    GMat pm, wm;
    GVec pos, rot;
    pos.x = 0; pos.y = 0; pos.z = -FX(9) / 2; pos.w = 0;
    rot.x = ax; rot.y = ay; rot.z = 0; rot.w = 0;
    g3->matrix_mode(c, G3M_PROJ);
    g3->mat_persp(&pm, FX(50), FX_ONE, FX(1), FX(16));
    g3->load(c, &pm);
    g3->matrix_mode(c, G3M_WORLD);
    g3->mat_trs(&wm, &pos, &rot, 0);
    g3->load(c, &wm);
    return g3->draw(c, G3P_TRIS, G3F_XYZ | G3F_NORMAL | G3F_DIFFUSE,
                    v, CUBE_VERTS, idx, CUBE_INDICES);
}
