/* Tracks which graphics service a fault disables. */
static inline unsigned gf_disable(unsigned vec, int in_gdi, int in_g3d,
                                  unsigned disabled)
{
    if (vec != 14 && vec != 13 && vec != 6) return 0;
    return (in_gdi ? 3u : in_g3d ? 2u : 0u) & ~disabled;
}
