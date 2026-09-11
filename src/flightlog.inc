/* Finds trace records that have not been saved yet. */
static inline int flight_slice(u32 seq, u32 len, u32 flushed,
                               u32 *off, u32 *count, u32 *lost)
{
    *off = 0; *count = 0; *lost = 0;
    if (flushed >= seq) return 0;
    u32 base = seq - len;
    if (flushed < base) {
        *lost = base - flushed;
        flushed = base;
    }
    *off   = flushed - base;
    *count = seq - flushed;
    return *count != 0;
}
