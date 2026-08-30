# Legacy MMR / Kind `I` Image Payloads

This note documents the legacy BookManager kind `I` payload format. The BOO
container stores these payloads as byte ranges referenced by the page-0 picture
directory; the container descriptor layout is documented in [assets.md](assets.md).

## Current Implementation Status

MMR support is implemented in `libgeist` for the observed legacy kind `I`
wrapper and the CCITT fax line streams verified against hosted
BookServer output. The public PNG renderer treats `legacy-mmr` as a renderable
format, the same way it treats `legacy-gdf`, while still reporting unsupported
or not-yet-mapped fax variants as clear render errors.

The implementation is self-contained. It uses the standard CCITT T.4/T.6
white/black run tables and 2D modes, with wrapper handling derived from the
fixtures and verified against hosted BookServer output. It does not embed or
copy third-party fax-decoder source.

## Payload Family

Legacy kind `I` payloads are a wrapper around CCITT fax (MMR) compressed
bitmap data. They are not GDF and not a standard image container: the stored
bytes begin with a fixed wrapper prefix, never with `GIF87a`, `GIF89a`, `BM`,
or a JPEG SOI marker. Every one of the 558 kind `I` descriptors across the
repository fixtures points at a payload starting `00 08 d3 a8 7b`.

The hosted BookServer serves these resources as GIFs generated on demand; see
[assets.md](assets.md). The GIF is the rendered artifact, so it is usable as an
independent check of a decoder's output but is not what the book stores.

## The Payload Is A Structured-Field Chain

A kind `I` payload is not a bespoke wrapper. It is a chain of length-prefixed
structured fields, all the way from byte 0 to the last byte the descriptor
declares, and every one of them has the same 8-byte header:

```c
struct BooImageStructuredField {
  uint16_t length_be;      // Whole field, header included.
  uint8_t  id[3];          // Field identifier.
  uint8_t  flags;          // 0x00, except 0x40 on picture-data fields.
  uint16_t sequence_be;    // 0x0000 in every field of every fixture.
  uint8_t  data[length - 8];
};
```

Five identifiers occur, and their EBCDIC-looking bytes are the identifiers of
the IBM image object architecture the payload is written in:

| `id` | Field | Occurrences in the corpus |
| --- | --- | ---: |
| `d3 a8 7b` | Begin image object | 559 |
| `d3 a7 7b` | Image output control | 559 |
| `d3 a6 7b` | Image input descriptor | 559 |
| `d3 ee 7b` | Image picture data | 858 |
| `d3 a9 7b` | End image object | 559 |

Measured over every kind `I` payload in the 35-book corpus: 559 payloads, one
begin/control/descriptor/end field each, and 858 picture-data fields — so some
images carry more than one data field. No other identifier occurs, no field
carries a non-zero sequence word, and the only non-zero flag byte is `0x40` on
picture data.

The whole prologue is therefore accounted for: three fields of 8, 32 and 32
bytes take a payload to offset `0x48`, where the first picture-data field's
8-byte header sits, so compressed bytes begin at `0x50`.

### The Prologue Is Byte-Identical Except The Dimensions

Blanking the two dimension words at `0x42` and `0x44`, the first `0x48` bytes
of all 559 kind `I` payloads in the corpus are **the same 72 bytes**:

```text
0000  00 08 d3 a8 7b 00 00 00   begin image object, no data
0008  00 20 d3 a7 7b 00 00 00   image output control, 24 bytes of data
0010  00 00 00 00 00 00 00 00
0018  2d 00 00 00 00 00 00 00
0020  00 00 03 e8 03 e8 ff ff
0028  00 20 d3 a6 7b 00 00 00   image input descriptor, 24 bytes of data
0030  01 01 00 64 00 64 00 00
0038  00 00 00 00 00 00 09 60
0040  09 60 ww ww hh hh 01 00
0048  LL LL d3 ee 7b 40 00 00   image picture data, LLLL bytes total
0050  ...                       compressed MMR bytes
```

The image input descriptor's 24 data bytes are the only per-image metadata,
and only two words of them vary:

| Payload offset | Descriptor data offset | Bytes | Meaning |
| ---: | ---: | --- | --- |
| `0x30` | 0 | `01` | Horizontal unit base. |
| `0x31` | 1 | `01` | Vertical unit base. |
| `0x32` | 2 | `00 64` | Horizontal units per unit base: `100`. Constant in all 559 payloads. |
| `0x34` | 4 | `00 64` | Vertical units per unit base: `100`. Constant in all 559 payloads. |
| `0x36` | 6 | eight zero bytes | Zero in all 559 payloads. |
| `0x3e` | 14 | `09 60` / 2400 | Constant in all 559 payloads. |
| `0x40` | 16 | `09 60` / 2400 | Constant in all 559 payloads. |
| `0x42` | 18 | per image | Bitmap width in pixels. |
| `0x44` | 20 | per image | Bitmap height in pixels. |
| `0x46` | 22 | `01 00` | Constant in all 559 payloads. |

`0x32` and `0x34` are the resolution unit counts, not dimensions. Earlier notes
here mistook them for width and height, and separately placed `09 60 09 60` at
`0x2e` and the picture-data identifier at `0x3a`; both offsets were sixteen
bytes low. The dimensions are at `0x42` and `0x44`, confirmed against the
hosted GIF for `GG24-4302-00.boo` resource `1` (960 x 832) and
`QSYSNEWG.BOO` resource `1` (205 x 413).

The corresponding legacy descriptor for that payload is:

```text
GG24-4302-00.boo
descriptor 0x0118:
f1 40 40 40 40 40 40 40 c9 00 1c fc 00 00 99 f0

id "1", kind I, length 0x001cfc, absolute payload offset 0x000099f0
```

Other kind `I` fixtures have the same wrapper shape with different dimensions,
which confirms that the dimension fields are per-resource and not constants:

| Fixture, resource | `0x42` width | `0x44` height | `0x46` | `0x48` first segment length |
| --- | ---: | ---: | --- | ---: |
| `GG24-4302-00.boo` `1` | `03 c0` / 960 | `03 40` / 832 | `01 00` | `0x1cac` |
| `GG24-4302-00.boo` `3` | `03 a0` / 928 | `03 60` / 864 | `01 00` | `0x105e` |
| `QSYSNEWG.BOO` `1` | `00 cd` / 205 | `01 9d` / 413 | `01 00` | `0x0517` |
| `SC33-033.boo` `2` | `04 e0` / 1248 | `02 40` / 576 | `01 00` | `0x0cee` |

Each of these four carries a single picture-data field, so its length is
`descriptor_length - 0x50`: the payload minus the 72-byte prologue and the
8-byte end-image-object field. A payload with more than one picture-data field
splits that budget between them.

## Segment Framing

The compressed data is carried in the picture-data fields of the chain above:

1. The image input descriptor begins at payload-relative `0x28`. Bitmap width
   and height are the big-endian words at payload-relative `0x42` and `0x44`
   (offsets `18` and `20` inside its data).
2. The first picture-data field's header is at payload-relative `0x48`. Its
   first word is the big-endian field length, counted from the start of the
   header and including the 8-byte header itself.
3. Compressed bytes for that field therefore start at `field + 8` --
   payload-relative `0x50` for the first one -- and run for `length - 8` bytes.
4. The next field starts at `field + length`, with the same shape. A payload may
   carry more than one: the corpus holds 858 picture-data fields across 559
   payloads. Feed their compressed bytes to the decoder in order.
5. The chain ends with an 8-byte end-image-object field (`d3 a9 7b`), which
   lands exactly on the end of the payload. **The terminator is a different
   field, not an empty picture-data field**; a decoder that only checks for
   length 8 will still stop in the right place, but one that checks the
   identifier will also catch a truncated payload.
6. The decoded bitmap is stored bottom-up: invert it vertically to obtain the
   image as rendered.

Verified by walking the chain of every kind `I` payload in the corpus: all 559
end on an 8-byte `d3 a9 7b` field whose last byte is the descriptor's last
byte, with no bytes left over and no field crossing the declared length.

## Fax Coding

The compressed segment body uses CCITT fax run-length coding. The coding is
compatible with the T.4/T.6 white and black terminating/makeup run tables used
by TIFF fax compression, but the BookManager wrapper is not a TIFF container.

Observed streams may begin each line with the 12-bit EOL code
`000000000001`, followed by a one-bit T.4 line tag:

| Tag bit after EOL | Meaning |
| --- | --- |
| `1` | The line is one-dimensional Modified Huffman / T.4 1D. |
| `0` | The line is two-dimensional Modified READ / T.4 2D, using the previous decoded line as reference. |

The two-dimensional path uses the standard pass, horizontal, and vertical codes
from CCITT T.6. Streams without per-line EOL markers are treated as T.6-style
2D MMR. A pure 1D MH fallback is retained for EOL-framed streams that do not
use a tag bit, but the hosted BookServer-backed fixture below uses the tagged
T.4 form.

The decoded reference line must be stored as alternating white/black run
lengths, not as absolute transition positions. This is the same run-pair model
TIFF fax decoders use, and it must validate monotonic run structure. Each
decoded line also carries the final imaginary zero-length run used by the next
2D line. The earlier absolute-transition model happened to work for simpler
resources, including `QSYSNEWG` resource `1`, but desynchronized on dense 2D
resources `12` and `56`.

The first line of `QSYSNEWG.BOO` resource `1` demonstrates the tag:

```text
payload 0x50: 00 1a e1 80...
bits:        000000000001 1 010111 000011 ...
             EOL          tag=1  white makeup 192 + white term 13
```

The first decoded line is therefore a full-width white line (`192 + 13 = 205`)
when the tag bit is consumed. Treating the bit after EOL as image data causes
the line to desynchronize.

## Reference Render

The hosted BookServer-backed fixture used for pixel-level validation is
`QSYSNEWG.BOO` resource `1`:

| Evidence | Value |
| --- | --- |
| Local fixture | `BOO/QSYSNEWG.BOO`, resource `1`, kind `I`, raw payload size `1383` bytes. |
| Hosted book | `BOOKS/QSYSNEWG`, `DT=19910524085706`, topic `1.1`. |
| Hosted artifact | `http://cbrdoc01.lan.cyber.gent/bookmgr/pictures/QSYSNEWG.19910524085706.P1.GIF` |
| Wrapper bitmap size | payload `0x42 = 00 cd` (`205`), payload `0x44 = 01 9d` (`413`). |
| First segment | payload `0x48 = 05 17`; compressed byte count `0x050f` after subtracting the 8-byte segment header. |
| BookServer GIF size | `82 x 165`. This is `floor(205 * 2 / 5)` by `floor(413 * 2 / 5)`. |

On 2026-06-18, `boorsrc --png BOO/QSYSNEWG.BOO 1` rendered a PNG that matched
the BookServer GIF at pixel level after normalizing the GIF to RGB PNG:

```text
dims: local 82x165, BookServer 82x165
pixel mismatches: 0 of 13530
```

The regression test `mmr_qsysnewg_test` renders this resource through
`BooDocument::read_resource_png()`, decodes the PNG to RGBA, and checks
dimensions `82 x 165` plus FNV-1a pixel hash `0x9491199eae92882e`.

A full `boo2git --force BOO/QSYSNEWG.BOO /tmp/geist-mmr/qsysnewg-boo2git`
smoke test now renders all 88 of the book's PNG resources through the same
public API. The two resources that previously failed are:

| Resource | Topic | Hosted artifact | Local dimensions | BookServer dimensions | Pixel comparison |
| --- | --- | --- | --- | --- | --- |
| `12` | `2.0` | `QSYSNEWG.19910524085706.P12.GIF` | `340 x 294` | `340 x 294` | `838` mismatches of `99960` pixels. |
| `56` | `6.0` | `QSYSNEWG.19910524085706.P56.GIF` | `344 x 385` | `344 x 385` | `450` mismatches of `132440` pixels. |

The remaining pixel deltas are sparse binary stroke differences after scaling,
not decode failures or dimension mismatches. Visual inspection of the local PNGs
and downloaded BookServer artifacts on 2026-06-18 judged resources `12` and
`56` visually identical for BookManager rendering purposes.

The hosted artifacts are scaled by two fifths, and their pixels are binary
rather than grayscale, so the scaling is not an area average. Nearest-neighbor
sampling at phase `(0,0)` is the closest match found against hosted output and
preserves the exact `P1` match above.

A decoder should treat a kind `I` payload as this structured-field chain, not
as a raw TIFF Group 4 stream: the field framing, the tagged-EOL line form, and
the bottom-up storage all differ from a bare Group 4 strip.

## Open Questions

- The constant metadata the prologue carries. `2d`, `03 e8 03 e8` and `ff ff`
  in the image output control field, and `01 01`, `00 64 00 64`, `09 60 09 60`
  and `01 00` in the image input descriptor, are identical in all 559 kind `I`
  payloads of the corpus, so nothing here can distinguish them. A book whose
  images were built at a different resolution would separate the unit-base and
  units-per-base fields from the rest.
- How a payload with more than one picture-data field decodes. 299 of the 559
  payloads carry more than one; the two fixtures validated against hosted
  artifacts at pixel level (`QSYSNEWG.BOO` resources `1`, `12`, `56` and
  `GG24-4302-00.boo` resource `1`) each carry exactly one, so whether the fax
  stream simply continues across the field boundary or each field restarts it
  is untested here. A pixel comparison of any multi-field resource against its
  hosted GIF would settle it.
- Audit more kind `I` books as fixtures are mapped to hosted BookServer
  artifacts, especially if future samples expose a picture-data field with a
  non-zero sequence word — none of the 858 in this corpus has one.
