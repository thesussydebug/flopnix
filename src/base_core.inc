/* Converts numbers between bases. */
#pragma once

static u32 bc_thousandths(u32 n,int shift)
{
    return (u32)(((unsigned long long)(n&((1u<<shift)-1))*1000u)>>shift);
}

static int bc_parse(const char *s, u32 base, u32 *out)
{
    u32 n = 0; int count = 0;
    if (base != 2 && base != 8 && base != 10 && base != 16) return -1;
    if (s[0] == '0' && ((base == 16 && (s[1]=='x'||s[1]=='X')) ||
        (base == 2 && (s[1]=='b'||s[1]=='B')) || (base == 8 && (s[1]=='o'||s[1]=='O')))) s += 2;
    while (*s) {
        char c = *s++; u32 d;
        if (c >= '0' && c <= '9') d = c-'0';
        else if (c >= 'a' && c <= 'f') d = c-'a'+10;
        else if (c >= 'A' && c <= 'F') d = c-'A'+10;
        else return -1;
        if (d >= base) return -1;
        if (n > (0xffffffffu-d)/base) return -2;
        n = n*base+d; count++;
    }
    if (!count) return 0;
    *out = n; return 1;
}

static void bc_format(u32 n, u32 base, char *out)
{
    char rev[32]; int k = 0;
    do { rev[k++] = "0123456789ABCDEF"[n%base]; n /= base; } while (n);
    for (int i = 0; i < k; i++) out[i] = rev[k-1-i];
    out[k] = 0;
}
