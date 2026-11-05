# PLANE — Packed Linear Array, Nothing Else

*We promise the file contains more than the name suggests.*

**Version 1** · extension `.plane` · little-endian · MIME `application/octet-stream`

## What PLANE is

A minimal container for a single 2D array of fixed-size chunks. The standard defines framing only: dimensions, chunk width, and payload layout. What the chunks *mean* is defined entirely by the writer and reader, out-of-band, through the flags field.

What the standard deliberately does **not** define:

- chunk meaning, channel count, color space
- compression
- animation or multiple planes per file
- magic bytes (error detection belongs to your container/packer)
- version numbers (versioning lives in flags)

## Header — 16 bytes, little-endian

```
byte:  0        4        8      9        13    16
       +--------+--------+------+--------+-----+-------- ...
       | width  | height |shift | flags  | rsv |  payload
       +--32----+--32----+--8---+--32----+-24--+
```


| offset | size | field      | meaning                                                        |
| ------ | ---- | ---------- | -------------------------------------------------------------- |
| 0      | 4    | `width`    | uint32 LE — chunks per row, &gt; 0                             |
| 4      | 4    | `height`   | uint32 LE — rows, &gt; 0                                       |
| 8      | 1    | `shift`    | uint8 — chunk is **1 &lt;&lt; shift** bits: 0→1-bit … 6→64-bit |
| 9      | 4    | `flags`    | uint32 LE — undefined by the standard, user-defined            |
| 13     | 3    | `reserved` | must be zero when writing; ignored when reading                |


Every value of `shift` is structurally legal (it is an exponent, so it cannot be malformed). Readers must sanity-check it anyway; this reference implementation caps at 6 (64-bit chunks). The three reserved bytes are the format's only headroom: writers must zero them, readers must ignore them, and a future version may define them.

## Payload

- Starts at **offset 16** — aligned for every chunk type.
- Row-major; each row padded to a whole byte.
- `stride = ceil(width << shift / 8)` bytes per row.
- Chunk `(x, y)` starts at bit `x << shift` within row `y`; row `y` starts at byte `y · stride`.
- File size is exactly `16 + height · stride` bytes. No trailing data.

**The addressing law:** because chunk widths are powers of two, **chunks never straddle byte boundaries.** For `shift < 3` the chunk fits inside one byte (mask within it); for `shift ≥ 3` it is a whole number of bytes starting on a byte boundary. There is no bitstream, no cross-byte stitching — one branch, one shift, one mask.

For `shift ≥ 3`, `stride == width << (shift − 3)` exactly, so the payload is tightly packed and casts directly to typed arrays for GPU upload. For `shift < 3` you lose at most 7 bits per row — pass `stride` as row pitch where the upload path accepts one.

## Chunk types (examples)


| `shift` | chunk  | typical use                      |
| ------- | ------ | -------------------------------- |
| 0       | 1-bit  | bitplane masks, occupancy        |
| 1       | 2-bit  | 4-entry palette indices          |
| 2       | 4-bit  | 16-entry palette indices         |
| 3       | 8-bit  | bytes, 256-entry palette indices |
| 4       | 16-bit | RGB 5:6:5, u16, f16              |
| 5       | 32-bit | RGBA8, u32, f32                  |
| 6       | 64-bit | u64, 2×f32                       |


## Conventions (not part of the standard)

- **Two-suffix naming:** keep the extension constant and put interpretation in the filename body: `hero_palette.plane`, `fog_mask.plane`, `voxel_ids.plane`. Every tool treats only the last suffix as the extension.
- **Private flags layout** is agreed between your writer and reader, e.g. low byte = channel count, next byte = interpretation enum, upper two bytes = version. The standard stays out of it, forever.

## Reference implementation — `plane.h`

Single header, dependency-free, C11. Loading is zero-copy: `Plane.data` points into your buffer.

```c
/* plane.h — PLANE: Packed Linear Array, Nothing Else. Version 1.
 * Little-endian. No magic. No version. Flags are yours. */
#ifndef PLANE_H
#define PLANE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define PLANE_HEADER_SIZE 16u
#define PLANE_MAX_SHIFT 6 /* chunks up to 64 bits; raise if you dare */

typedef struct {
    uint32_t width, height; /* chunks per row, rows */
    uint8_t  shift;    /* log2 bits per chunk: chunk = 1u << shift */
    uint32_t flags;     /* user-defined */
    uint8_t *data;     /* payload rows, each plane_stride(width, shift) bytes */
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
```

## Example

```c
Plane m;
if (plane_alloc(&m, 8, 8, 0)) {        /* 8x8 bitplane mask */
    plane_set(&m, 3, 5, 1);
    assert(plane_get(&m, 3, 5) == 1);
    m.flags = 1;                         /* your convention: 1 channel */
    plane_save(&m, "fog_mask.plane");
    plane_free(&m);
}
```

## Design rationale, in one paragraph

The header is 16 bytes so the payload is aligned for every chunk type. `shift` stores the exponent rather than the width (Linux-kernel style, cf. `PAGE_SHIFT`), so no value can be malformed and all addressing collapses to shifts. Chunk widths are powers of two so nothing ever straddles a byte — the codec floor is "shift, mask, multiply," roughly 15 lines of logic. The standard refuses self-description on purpose: flags carry all semantics, which keeps the format generic enough for masks, palette indices, voxel ID maps and paint layers alike, and means the spec never needs a version bump for new content types.
