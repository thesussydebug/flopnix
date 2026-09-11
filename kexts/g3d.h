/* Types and calls for the 3D drawing service. */
#pragma once
#include "gdi.h"

typedef struct { fx m[16]; } GMat;
typedef struct { fx x, y, z, w; } GVec;
typedef struct { fx x, y, z; u32 diffuse; } G3Vtx;
typedef struct G3D G3D;

enum { G3P_POINTS, G3P_LINES, G3P_LINESTRIP, G3P_TRIS, G3P_TRISTRIP, G3P_TRIFAN };
enum { G3M_WORLD, G3M_VIEW, G3M_PROJ, G3M_TEX };
enum { G3F_XYZ = 1, G3F_NORMAL = 2, G3F_DIFFUSE = 4, G3F_TEX1 = 8 };
enum { G3S_ZENABLE, G3S_ZWRITE, G3S_ZFUNC, G3S_CULL, G3S_SHADE, G3S_FILL,
       G3S_FOG, G3S_FOGCOLOR, G3S_FOGSTART, G3S_FOGEND, G3S_TEXFILTER,
       G3S_TEXPERSP, G3S_MIPMAP, G3S_ALPHATEST, G3S_LIGHTING, G3S_AMBIENT,
       G3S_DITHER, G3S_SUBDIV, G3S_MAX };
enum { G3FILL_POINT, G3FILL_WIRE, G3FILL_SOLID };
enum { G3SHADE_FLAT, G3SHADE_GOURAUD };
enum { G3CULL_NONE, G3CULL_CW, G3CULL_CCW };

typedef struct { u32 in, clipped, culled, drawn, spans, pixels, us; } G3Stats;

typedef struct { GVec dir; fx diffuse; } G3Light;
typedef struct { fx ambient, diffuse; } G3Mtl;
typedef struct { fx x, y, z, nx, ny, nz; u32 diffuse; } G3VtxN;

#define G3D_ABI 3
typedef struct {
    u32 abi;

    G3D *(*create)(void);
    void (*destroy)(G3D *);
    void (*viewport)(G3D *, int x, int y, int w, int h, fx zn, fx zf);
    void (*clear)(G3D *, GRGB c);

    void (*matrix_mode)(G3D *, int mode);
    void (*identity)(G3D *);
    void (*load)(G3D *, const GMat *);
    void (*mult)(G3D *, const GMat *);
    void (*push)(G3D *);
    void (*pop)(G3D *);

    void (*set)(G3D *, int state, u32 v);
    u32  (*get)(G3D *, int state);

    int  (*draw)(G3D *, int prim, u32 fvf, const void *v, int nv,
                 const u16 *idx, int ni);

    int  (*project)(G3D *, const GVec *in, int n, GVec *out);
    int  (*stats)(G3D *, G3Stats *);

    void (*mat_mul)(GMat *out, const GMat *a, const GMat *b);
    void (*mat_persp)(GMat *, fx fovy_deg, fx aspect, fx zn, fx zf);
    void (*mat_rot_xyz)(GMat *, fx ax_deg, fx ay_deg, fx az_deg);
    void (*mat_trs)(GMat *, const GVec *t, const GVec *r_deg, const GVec *s);

    void (*clearz)(G3D *, fx zndc);

    int  (*light)(G3D *, int i, const G3Light *l);
    void (*material)(G3D *, const G3Mtl *m);
    void (*mat_lookat)(GMat *, const GVec *eye, const GVec *at, const GVec *up);
} G3dOps;

static inline const G3dOps *g3d_bind(const Kapi *k, u32 min_abi)
{
    const G3dOps *g = (const G3dOps *)k->service_get("g3d");
    return (g && g->abi >= min_abi) ? g : 0;
}
