/* Classifies floppy read results. */
#pragma once

enum { FH_UNTESTED = 0, FH_OK, FH_SLOW, FH_RETRY, FH_BAD };

#define FH_SLOW_MS  250
#define FH_CYLS     80
#define FH_HEADS    2
#define FH_SECTORS  18
#define FH_TOTAL    (FH_CYLS * FH_HEADS * FH_SECTORS)

static inline int fh_class(int rc, int attempts, u32 ms)
{
    if (rc != 0) return FH_BAD;
    if (attempts > 1) return FH_RETRY;
    return ms >= FH_SLOW_MS ? FH_SLOW : FH_OK;
}

static inline int fh_worst(int a, int b)
{
    return a > b ? a : b;
}

static inline int fh_cell(u32 lba, int *cyl, int *head)
{
    if (lba >= FH_TOTAL) return 0;
    if (cyl)  *cyl  = (int)(lba / (FH_HEADS * FH_SECTORS));
    if (head) *head = (int)((lba / FH_SECTORS) % FH_HEADS);
    return 1;
}

static inline int fh_verdict(u32 bad, u32 retry, u32 slow, u32 total)
{
    if (bad) return 2;
    if (retry) return 1;
    if (total && slow * 100 / total >= 5) return 1;
    return 0;
}
