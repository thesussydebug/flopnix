/* Finds the changed pixels in one screen row. */
static inline int fb_span(const u8 *now, const u8 *last, int width, int *left, int *len)
{
    int a = 0, b = width;
    typedef u32 FbWord __attribute__((may_alias, aligned(1)));
    while (a + 4 <= b && *(const FbWord *)(now + a) == *(const FbWord *)(last + a)) a += 4;
    while (a < b && now[a] == last[a]) a++;
    if (a == b) { *left = *len = 0; return 0; }
    while (b - a >= 4 && *(const FbWord *)(now + b - 4) == *(const FbWord *)(last + b - 4)) b -= 4;
    while (b > a && now[b - 1] == last[b - 1]) b--;
    *left = a; *len = b - a;
    return 1;
}
