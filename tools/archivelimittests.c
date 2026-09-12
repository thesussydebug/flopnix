#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kapi.h"
#include "archive_core.inc"

static int pass, fail;
#define CHECK(x) do { if (x) pass++; else { fail++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

static u32 stored_entry(u8 *archive, u32 off, const char *name, const u8 *raw, u32 n)
{
    memset(archive + off, 0, 28);
    memcpy(archive + off, name, strlen(name));
    lz_put32(archive + off + 24, LZ_HDR + n);
    lz_head(archive + off + 28, n, lz_csum(raw, n), 1);
    memcpy(archive + off + 28 + LZ_HDR, raw, n);
    return off + 28 + LZ_HDR + n;
}

static void roundtrip(u8 *source, u8 *packed, u8 *decoded, u32 n, int random)
{
    u32 state = 0x7148a521u;
    for (u32 i = 0; i < n; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        source[i] = random ? (u8)state : (u8)(i % 251);
    }
    memset(packed, 0xa5, n + LZ_HDR + 1);
    memset(decoded, 0x5a, n + 1);
    u32 size = lz_pack(source, n, packed, n + LZ_HDR);
    CHECK(size >= LZ_HDR && size <= n + LZ_HDR);
    CHECK(packed[n + LZ_HDR] == 0xa5);
    CHECK(lz_get32(packed + 4) == n);
    CHECK(random ? packed[10] == 1 : packed[10] == 0);
    CHECK(lz_unpack(packed, size, decoded, n) == (int)n);
    CHECK(memcmp(source, decoded, n) == 0);
    CHECK(decoded[n] == 0x5a);
    CHECK(lz_unpack(packed, size, decoded, n - 1) < 0);
    CHECK(lz_pack(source, n, packed, LZ_HDR) == 0);
}

int main(void)
{
    CHECK(AR_FILE == 131072u);
    CHECK(AR_CAP == 1048576u);
    CHECK(ar_buffer_limit(0) == 131072u);
    CHECK(ar_buffer_limit(4096) == 131072u);
    CHECK(ar_buffer_limit(7168) == 131072u);
    CHECK(ar_buffer_limit(8064) == 262144u);
    CHECK(ar_buffer_limit(8191) == 262144u);
    CHECK(ar_buffer_limit(8192) == 262144u);
    CHECK(ar_buffer_limit(15360) == 262144u);
    CHECK(ar_buffer_limit(16256) == 524288u);
    CHECK(ar_buffer_limit(16383) == 524288u);
    CHECK(ar_buffer_limit(16384) == 524288u);
    CHECK(ar_buffer_limit(31744) == 524288u);
    CHECK(ar_buffer_limit(32640) == 1048576u);
    CHECK(ar_buffer_limit(32767) == 1048576u);
    CHECK(ar_buffer_limit(32768) == 1048576u);
    CHECK(ar_buffer_limit(0xffffffffu) == 1048576u);

    u8 *raw = malloc(131073u);
    u8 *packed = malloc(131072u + LZ_HDR + 1);
    u8 *decoded = malloc(131073u);
    u8 *archive = malloc(1048577u);
    if (!raw || !packed || !decoded || !archive) {
        fputs("Allocation failed\n", stderr);
        free(raw); free(packed); free(decoded); free(archive);
        return 1;
    }
    const u32 sizes[] = {65535u, 65536u, 65537u, 131072u};
    for (int i = 0; i < 4; i++) {
        roundtrip(raw, packed, decoded, sizes[i], 0);
        roundtrip(raw, packed, decoded, sizes[i], 1);
    }
    ArEntry entries[AR_FILES];
    ar_empty(archive);
    archive[4] = 1;
    u32 n = stored_entry(archive, 8, "full.bin", raw, 131072u);
    CHECK(n > 131072u);
    CHECK(ar_index(archive, n, entries) == 1);
    CHECK(entries[0].raw == 131072u && entries[0].offset == 36);
    CHECK(lz_unpack(archive + entries[0].offset, entries[0].packed, decoded, 131072u) == 131072);
    CHECK(memcmp(raw, decoded, 131072u) == 0);
    CHECK(ar_index(archive, n - 1, entries) < 0);
    archive[n] = 0;
    CHECK(ar_index(archive, n + 1, entries) < 0);

    raw[131072] = 0x93;
    n = stored_entry(archive, 8, "oversize.bin", raw, 131073u);
    CHECK(ar_index(archive, n, entries) < 0);

    ar_empty(archive);
    archive[4] = 2;
    n = stored_entry(archive, 8, "first.bin", raw, 65536u);
    n = stored_entry(archive, n, "second.bin", raw + 65536, 65536u);
    CHECK(n > 131072u);
    CHECK(ar_index(archive, n, entries) == 2);
    CHECK(entries[0].raw == 65536u && entries[1].raw == 65536u);

    ar_empty(archive);
    archive[4] = 1;
    n = stored_entry(archive, 8, "one.bin", raw, 131072u);
    archive[4] = 2;
    n = stored_entry(archive, n, "two.bin", raw, 131072u);
    CHECK(n > ar_buffer_limit(8192));
    CHECK(n < ar_buffer_limit(16384));
    CHECK(ar_index(archive, n, entries) == 2);
    ar_empty(archive);
    archive[4] = 8;
    n = 8;
    for (int i = 0; i < 8; i++) {
        char name[24];
        snprintf(name, sizeof name, "part%d.bin", i);
        u32 bytes = i == 7 ? AR_CAP - n - 28 - LZ_HDR : 131072u;
        n = stored_entry(archive, n, name, raw, bytes);
    }
    CHECK(n == AR_CAP);
    CHECK(ar_index(archive, n, entries) == 8);
    CHECK(ar_index(archive, AR_CAP + 1, entries) < 0);

    free(raw); free(packed); free(decoded); free(archive);
    printf("ARCHIVE LIMITS: %d pass %d fail\n", pass, fail);
    return fail != 0;
}
