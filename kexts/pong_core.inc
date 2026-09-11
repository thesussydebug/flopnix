/* Updates paddle movement and ball collisions. */
#define PONG_W  320
#define PONG_H  200
#define PONG_PH 40
#define PONG_PW 6
#define PONG_PX 10
#define PONG_B  6
#define PONG_V0   (3 << 8)
#define PONG_VINC (1 << 7)
#define PONG_VMAX (6 << 8)
#define PONG_VYMX (4 << 8)

#define PONG_HIT_WALL   1
#define PONG_HIT_PADDLE 2

typedef struct {
    i32 bx, by;
    i32 vx, vy;
    int pl, pr;
    int hit;
} PongSt;

static inline void pong_serve(PongSt *s, int side)
{
    s->bx = (PONG_W / 2 - PONG_B / 2) << 8;
    s->by = (PONG_H / 2 - PONG_B / 2) << 8;
    s->vx = side < 0 ? -PONG_V0 : PONG_V0;
    s->vy = 1 << 7;
    s->pl = PONG_H / 2 - PONG_PH / 2;
    s->pr = PONG_H / 2 - PONG_PH / 2;
}

static inline int pong_paddle(PongSt *s, int ptop)
{
    int by = s->by >> 8;
    if (by + PONG_B < ptop || by > ptop + PONG_PH) return 0;

    int mag = s->vx < 0 ? -s->vx : s->vx;
    mag += PONG_VINC;
    if (mag > PONG_VMAX) mag = PONG_VMAX;
    s->vx = s->vx > 0 ? -mag : mag;

    int off = (by + PONG_B / 2) - (ptop + PONG_PH / 2);
    s->vy += off << 4;
    if (s->vy > PONG_VYMX) s->vy = PONG_VYMX;
    if (s->vy < -PONG_VYMX) s->vy = -PONG_VYMX;
    return 1;
}

static inline int pong_step(PongSt *s)
{
    s->hit = 0;
    s->bx += s->vx;
    s->by += s->vy;

    if (s->by < 0) { s->by = 0; s->vy = -s->vy; s->hit |= PONG_HIT_WALL; }
    if (s->by > (PONG_H - PONG_B) << 8) {
        s->by = (PONG_H - PONG_B) << 8;
        s->vy = -s->vy;
        s->hit |= PONG_HIT_WALL;
    }

    int bx = s->bx >> 8;
    int lface = PONG_PX + PONG_PW;
    int rface = PONG_W - PONG_PX - PONG_PW;
    if (s->vx < 0 && bx <= lface && bx + PONG_B >= PONG_PX) {
        if (pong_paddle(s, s->pl)) { s->bx = lface << 8; s->hit |= PONG_HIT_PADDLE; }
    } else if (s->vx > 0 && bx + PONG_B >= rface && bx <= rface + PONG_PW) {
        if (pong_paddle(s, s->pr)) {
            s->bx = (rface - PONG_B) << 8;
            s->hit |= PONG_HIT_PADDLE;
        }
    }

    if (s->bx < -(PONG_B << 8)) return 2;
    if (s->bx > (PONG_W << 8)) return 1;
    return 0;
}

static inline void pong_ai(PongSt *s, int maxstep)
{
    int want = (s->by >> 8) + PONG_B / 2 - PONG_PH / 2;
    int d = want - s->pr;
    if (d > maxstep) d = maxstep;
    if (d < -maxstep) d = -maxstep;
    s->pr += d;
    if (s->pr < 0) s->pr = 0;
    if (s->pr > PONG_H - PONG_PH) s->pr = PONG_H - PONG_PH;
}
