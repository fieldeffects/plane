# PLANE — Packed Linear Array, Nothing Else

*ile contains more than the name suggests.*

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
