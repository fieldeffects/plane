/* test_plane.c — unit tests for plane.h (PLANE v1 reference implementation).
 * Build: cc -std=c11 -Wall -Wextra -Werror -I.. test_plane.c -o test_plane
 * Run from tests/ so the round-trip fixture lands in a temp path. */
#include "../plane.h"

#include <assert.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++failures; \
    } \
} while (0)

/* --- geometry ---------------------------------------------------------- */
static void test_stride(void) {
    CHECK(plane_stride(8, 0)  == 1);            /* 8 bits  = 1 byte        */
    CHECK(plane_stride(9, 0)  == 2);            /* 9 bits  = 2 bytes       */
    CHECK(plane_stride(1, 0)  == 1);            /* 1 bit   = 1 byte (padded) */
    CHECK(plane_stride(4, 2)  == 2);            /* 16 bits = 2 bytes       */
    CHECK(plane_stride(3, 5)  == 12);           /* 96 bits = 12 bytes      */
    CHECK(plane_stride(1, 6)  == 8);            /* 64 bits = 8 bytes       */
    CHECK(plane_stride(0xFFFFFFFFu, 6) == ((size_t)0xFFFFFFFFu << 3)); /* no overflow */
    CHECK(plane_payload_size(3, 4, 5) == 48);
}

/* --- chunk access, every shift ----------------------------------------- */
static void test_get_set_all_shifts(void) {
    for (uint8_t shift = 0; shift <= PLANE_MAX_SHIFT; ++shift) {
        const uint32_t w = 13, h = 7;           /* odd sizes: exercises padding */
        const uint64_t chunk_mask = (shift == 6) ? ~0ull : ((1ull << (1u << shift)) - 1);
        Plane p;
        CHECK(plane_alloc(&p, w, h, shift));
        CHECK(p.flags == 0);

        /* alloc zero-fills */
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                CHECK(plane_get(&p, x, y) == 0);

        /* fill every chunk with a distinct pattern, read back */
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                plane_set(&p, x, y, (uint64_t)(x * 131u + y * 17u + 1u) & chunk_mask);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                CHECK(plane_get(&p, x, y) == ((uint64_t)(x * 131u + y * 17u + 1u) & chunk_mask));

        /* saturate one chunk; neighbours must not bleed */
        plane_set(&p, 5, 3, chunk_mask);
        CHECK(plane_get(&p, 5, 3) == chunk_mask);
        if (5 > 0)     CHECK(plane_get(&p, 4, 3) == ((uint64_t)(4 * 131u + 3 * 17u + 1u) & chunk_mask));
        if (5 < w - 1) CHECK(plane_get(&p, 6, 3) == ((uint64_t)(6 * 131u + 3 * 17u + 1u) & chunk_mask));
        if (3 > 0)     CHECK(plane_get(&p, 5, 2) == ((uint64_t)(5 * 131u + 2 * 17u + 1u) & chunk_mask));
        if (3 < h - 1) CHECK(plane_get(&p, 5, 4) == ((uint64_t)(5 * 131u + 4 * 17u + 1u) & chunk_mask));

        /* values wider than the chunk are truncated, not stored */
        plane_set(&p, 0, 0, ~0ull);
        CHECK(plane_get(&p, 0, 0) == chunk_mask);

        plane_free(&p);
        CHECK(p.data == NULL);
    }
}

/* --- known bit patterns (shift < 3 packs inside a byte) ----------------- */
static void test_bit_packing_vectors(void) {
    Plane p;
    CHECK(plane_alloc(&p, 8, 1, 0));           /* 8x1 bitplane: one byte   */
    plane_set(&p, 0, 0, 1); plane_set(&p, 3, 0, 1); plane_set(&p, 7, 0, 1);
    CHECK(p.data[0] == 0x89);                   /* 1000 1001 */
    CHECK(plane_get(&p, 1, 0) == 0);
    plane_free(&p);

    /* shift=2: 4-bit chunks, chunk x at bit 4x */
    CHECK(plane_alloc(&p, 2, 1, 2));            /* 2 nibbles: one byte      */
    plane_set(&p, 0, 0, 0xE); plane_set(&p, 1, 0, 0x4);
    CHECK(p.data[0] == 0x4E);                   /* low nibble E, high nibble 4 */
    plane_free(&p);

    CHECK(plane_alloc(&p, 4, 1, 2));            /* 4 nibbles: two bytes     */
    plane_set(&p, 0, 0, 0xE); plane_set(&p, 1, 0, 0x4);
    plane_set(&p, 2, 0, 0x2); plane_set(&p, 3, 0, 0x1);
    CHECK(p.data[0] == 0x4E && p.data[1] == 0x12);
    plane_free(&p);

    CHECK(plane_alloc(&p, 2, 1, 1));            /* 2x1 2-bit: one byte      */
    plane_set(&p, 0, 0, 0x3); plane_set(&p, 1, 0, 0x1);
    CHECK(p.data[0] == (uint8_t)(0x3 | (0x1 << 2)));
    plane_free(&p);
}

/* --- whole-byte chunks (shift >= 3): little-endian payload -------------- */
static void test_byte_layout(void) {
    Plane p;
    CHECK(plane_alloc(&p, 2, 2, 4));            /* 16-bit chunks            */
    plane_set(&p, 0, 0, 0xBEEF);
    plane_set(&p, 1, 0, 0x1234);
    plane_set(&p, 0, 1, 0x00FF);
    plane_set(&p, 1, 1, 0xABCD);
    /* stride = 4; row-major, LE */
    CHECK(p.data[0] == 0xEF && p.data[1] == 0xBE);
    CHECK(p.data[2] == 0x34 && p.data[3] == 0x12);
    CHECK(p.data[4] == 0xFF && p.data[5] == 0x00);
    CHECK(p.data[6] == 0xCD && p.data[7] == 0xAB);
    CHECK(plane_get(&p, 1, 1) == 0xABCD);
    plane_free(&p);

    CHECK(plane_alloc(&p, 1, 1, 6));            /* single 64-bit chunk      */
    plane_set(&p, 0, 0, 0x0102030405060708ull);
    for (int i = 0; i < 8; ++i) CHECK(p.data[i] == (uint8_t)(8 - i)); /* LE */
    CHECK(plane_get(&p, 0, 0) == 0x0102030405060708ull);
    plane_free(&p);
}

/* --- alloc validation ---------------------------------------------------- */
static void test_alloc_validation(void) {
    Plane p;
    CHECK(!plane_alloc(&p, 0, 1, 0));
    CHECK(!plane_alloc(&p, 1, 0, 0));
    CHECK(!plane_alloc(&p, 1, 1, PLANE_MAX_SHIFT + 1));
    CHECK(plane_alloc(&p, 1, 1, PLANE_MAX_SHIFT));
    plane_free(&p);
}

/* --- save / load round-trip ---------------------------------------------- */
static void test_save_load_roundtrip(void) {
    const char *path = "test_roundtrip.plane";
    Plane a;
    CHECK(plane_alloc(&a, 19, 5, 3));           /* 8-bit chunks, odd width  */
    a.flags = 0xDEADBEEFu;
    for (uint32_t y = 0; y < a.height; ++y)
        for (uint32_t x = 0; x < a.width; ++x)
            plane_set(&a, x, y, (uint64_t)(x * 7u + y) & 0xFFu);
    CHECK(plane_save(&a, path));

    /* file size is exactly 16 + height*stride, no trailing data */
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    const long fsz = ftell(f);
    CHECK((size_t)fsz == plane_file_size(&a));
    CHECK((size_t)fsz == 16 + 5 * 19);
    uint8_t *buf = malloc((size_t)fsz);
    CHECK(buf != NULL);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    CHECK(fread(buf, 1, (size_t)fsz, f) == (size_t)fsz);
    fclose(f);

    /* header fields land at the specified offsets, reserved zeroed */
    CHECK(buf[0] == 19 && buf[1] == 0 && buf[2] == 0 && buf[3] == 0);
    CHECK(buf[4] == 5);
    CHECK(buf[8] == 3);
    CHECK(buf[9] == 0xEF && buf[10] == 0xBE && buf[11] == 0xAD && buf[12] == 0xDE);
    CHECK(buf[13] == 0 && buf[14] == 0 && buf[15] == 0);

    Plane b;
    CHECK(plane_load(&b, buf, (size_t)fsz));
    CHECK(b.width == a.width && b.height == a.height);
    CHECK(b.shift == a.shift && b.flags == a.flags);
    CHECK(b.data == buf + PLANE_HEADER_SIZE);   /* zero-copy */
    for (uint32_t y = 0; y < a.height; ++y)
        for (uint32_t x = 0; x < a.width; ++x)
            CHECK(plane_get(&b, x, y) == plane_get(&a, x, y));

    /* reserved bytes are ignored on read even when non-zero */
    buf[13] = 0xAA; buf[14] = 0xBB; buf[15] = 0xCC;
    Plane c;
    CHECK(plane_load(&c, buf, (size_t)fsz));
    CHECK(c.width == a.width && c.flags == a.flags);

    free(buf);
    plane_free(&a);
    remove(path);
}

/* --- load rejects malformed input ---------------------------------------- */
static void test_load_validation(void) {
    uint8_t buf[64];
    memset(buf, 0, sizeof buf);
    Plane p;

    CHECK(!plane_load(&p, NULL, sizeof buf));
    CHECK(!plane_load(&p, buf, PLANE_HEADER_SIZE - 1));   /* too short        */

    /* width = 0 */
    CHECK(!plane_load(&p, buf, sizeof buf));
    /* height = 0 */
    buf[4] = 1;                                            /* width  = 0 still  */
    CHECK(!plane_load(&p, buf, sizeof buf));
    /* shift > PLANE_MAX_SHIFT */
    buf[0] = 1; buf[4] = 1; buf[8] = PLANE_MAX_SHIFT + 1;
    CHECK(!plane_load(&p, buf, sizeof buf));
    /* payload truncated: 1x1 at shift 5 needs 4 bytes, give 3 */
    buf[0] = 1; buf[4] = 1; buf[8] = 5;
    CHECK(!plane_load(&p, buf, PLANE_HEADER_SIZE + 3));
    CHECK(plane_load(&p, buf, PLANE_HEADER_SIZE + 4));     /* exact fit is OK  */
    /* extra trailing data is tolerated by load (file-size rule is on writers) */
    CHECK(plane_load(&p, buf, sizeof buf));
}

int main(void) {
    test_stride();
    test_get_set_all_shifts();
    test_bit_packing_vectors();
    test_byte_layout();
    test_alloc_validation();
    test_save_load_roundtrip();
    test_load_validation();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all plane.h tests passed\n");
    return 0;
}
