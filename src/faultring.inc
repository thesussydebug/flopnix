/* Indexes the recent fault records. */
#define FAULT_HIST 12

static int fault_ring_count(u32 total)
{
    return total > FAULT_HIST ? FAULT_HIST : (int)total;
}

static int fault_ring_slot(int i, u32 total)
{
    int n = fault_ring_count(total);
    if (i < 0 || i >= n) return -1;

    int newest = (int)((total - 1) % FAULT_HIST);
    int s = newest - i;
    if (s < 0) s += FAULT_HIST;
    return s;
}
