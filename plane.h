/* plane.h — PLANE: Packed Linear Array, Nothing Else.
 * Little-endian. No magic. No version. Flags are yours. */
#ifndef PLANE_H
#define PLANE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define PLANE_HEADER_SIZE 16u
#define PLANE_MAX_SHIFT 6       /* chunks up to 64 bits; raise if you dare */

typedef struct {
    uint32_t    width;          /* chunks per row, rows */
    uint32_t    height;         /* rows */
    uint8_t     shift;          /* log2 bits per chunk: chunk = 1u << shift */
    uint32_t    flags;          /* user-defined */
    uint8_t*    data;           /* payload rows, each plane_stride(width, shift) bytes */
} Plane;

/* geometry — all functions assume 0 <= shift <= PLANE_MAX_SHIFT */
static inline size_t plane_stride(uint32_t width, uint8_t shift) {
    return (size_t)((((uint64_t)width << shift) + 7) >> 3);
}
static inline size_t plane_payload_size(uint32_t width, uint32_t height, uint8_t shift) {
    return (size_t)height * plane_stride(width, shift);
}
static inline size_t plane_file_size(const Plane *p) {
    return PLANE_HEADER_SIZE + plane_payload_size(p->width, p->height, p->shift);
}

/* chunk access — power-of-2 widths never straddle a byte */
static inline uint64_t plane_get(const Plane *p, uint32_t x, uint32_t y) {
    const size_t bits   = (size_t)1 << p->shift;
    const size_t bitoff = (size_t)x << p->shift;
    const uint8_t *b    = p->data + (size_t)y * plane_stride(p->width, p->shift) + (bitoff >> 3);
    if (bits <= 8) {                       /* inside one byte */
        const uint64_t mask = ((uint64_t)1 << bits) - 1;
        return (*b >> (bitoff & 7)) & mask;
    }
    uint64_t v = 0;                        /* whole bytes */
    for (size_t i = 0; i < (bits >> 3); ++i) v |= (uint64_t)b[i] << (8 * i);
    return v;
}

static inline void plane_set(Plane *p, uint32_t x, uint32_t y, uint64_t v) {
    const size_t bits   = (size_t)1 << p->shift;
    const size_t bitoff = (size_t)x << p->shift;
    uint8_t *b = p->data + (size_t)y * plane_stride(p->width, p->shift) + (bitoff >> 3);
    if (bits <= 8) {
        const uint64_t mask = ((uint64_t)1 << bits) - 1;
        const unsigned sh = (unsigned)(bitoff & 7);
        *b = (uint8_t)((*b & ~(uint8_t)(mask << sh))
                     | (uint8_t)((v & mask) << sh));
        return;
    }
    for (size_t i = 0; i < (bits >> 3); ++i) b[i] = (uint8_t)(v >> (8 * i));
}

/* create + destroy */
static inline bool plane_alloc(Plane *p, uint32_t width, uint32_t height, uint8_t shift) {
    if (width == 0 || height == 0 || shift > PLANE_MAX_SHIFT) return false;
    p->width = width; p->height = height; p->shift = shift;
    p->flags = 0;
    p->data = (uint8_t *)calloc((size_t)height, plane_stride(width, shift));
    return p->data != NULL;
}
static inline void plane_free(Plane *p) { free(p->data); p->data = NULL; }

/* IO — load is zero-copy: p->data points into buf */
static inline bool plane_load(Plane *p, uint8_t *buf, size_t len) {
    if (!p || !buf || len < PLANE_HEADER_SIZE) return false;
    p->width = (uint32_t)buf[0] | (uint32_t)buf[1] << 8
             | (uint32_t)buf[2] << 16 | (uint32_t)buf[3] << 24;
    p->height = (uint32_t)buf[4] | (uint32_t)buf[5] << 8
              | (uint32_t)buf[6] << 16 | (uint32_t)buf[7] << 24;
    p->shift = buf[8];
    p->flags = (uint32_t)buf[9]  | (uint32_t)buf[10] << 8
             | (uint32_t)buf[11] << 16 | (uint32_t)buf[12] << 24;
    /* bytes 13–15 reserved: ignore */
    if (p->width == 0 || p->height == 0 || p->shift > PLANE_MAX_SHIFT) return false;
    if (len - PLANE_HEADER_SIZE < plane_payload_size(p->width, p->height, p->shift)) return false;
    p->data = buf + PLANE_HEADER_SIZE;
    return true;
}

static inline bool plane_save(const Plane *p, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t hdr[PLANE_HEADER_SIZE];
    hdr[0] = (uint8_t)p->width;         hdr[1] = (uint8_t)(p->width >> 8);
    hdr[2] = (uint8_t)(p->width >> 16); hdr[3] = (uint8_t)(p->width >> 24);
    hdr[4] = (uint8_t)p->height;        hdr[5] = (uint8_t)(p->height >> 8);
    hdr[6] = (uint8_t)(p->height >> 16); hdr[7] = (uint8_t)(p->height >> 24);
    hdr[8] = p->shift;
    hdr[9] = (uint8_t)p->flags;           hdr[10] = (uint8_t)(p->flags >> 8);
    hdr[11] = (uint8_t)(p->flags >> 16);  hdr[12] = (uint8_t)(p->flags >> 24);
    hdr[13] = hdr[14] = hdr[15] = 0;      /* reserved */
    const size_t n = plane_payload_size(p->width, p->height, p->shift);
    const bool ok = fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr
                 && fwrite(p->data, 1, n, f) == n;
    fclose(f);
    return ok;
}

#endif /* PLANE_H */
