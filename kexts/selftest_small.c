/* Runs a smaller guest test suite for low-memory machines. */
#define SELFTEST_SMALL 1
#include "selftest.c"

void *memcpy(void *dst,const void *src,__SIZE_TYPE__ n)
{
    unsigned char *d=dst;const unsigned char *s=src;
    for(__SIZE_TYPE__ i=0;i<n;i++)d[i]=s[i];return dst;
}
